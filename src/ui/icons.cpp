// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.h"

#include "components.h"
#include "theme.h"
#include "../net/transit_api.h"

#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t((r & 0xf8) << 8) | uint16_t((g & 0xfc) << 3) | uint16_t(b >> 3);
}

constexpr uint16_t S_GREEN = rgb(0, 141, 88);
constexpr uint16_t U_BLUE = rgb(0, 102, 174);
constexpr uint16_t TRAM_RED = rgb(226, 0, 26);
constexpr uint16_t BUS_PURPLE = rgb(165, 0, 125);
constexpr uint16_t FERRY_CYAN = rgb(0, 155, 213);

enum class Sky : uint8_t { Sun, Haze, Cloud, Fog, Rain, Snow, Thunder };

Sky skyFor(int wmo) {
    if (wmo == 0) return Sky::Sun;
    if (wmo >= 1 && wmo <= 2) return Sky::Haze;
    if (wmo == 3) return Sky::Cloud;
    if (wmo == 45 || wmo == 48) return Sky::Fog;
    if (wmo >= 95) return Sky::Thunder;
    if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return Sky::Snow;
    if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) return Sky::Rain;
    return Sky::Cloud;
}

int unit(int value, int size) { return (value * size + 16) / 32; }

void sun(int x, int y, int size, bool partial) {
    const int cx = x + unit(partial ? 23 : 16, size);
    const int cy = y + unit(partial ? 8 : 15, size);
    const int radius = max(2, unit(partial ? 5 : 7, size));
    const uint16_t amber = getWarningColor();
    tft.fillCircle(cx, cy, radius, amber);
    if (partial) return;
    const int inner = radius + max(2, size / 10);
    const int outer = inner + max(2, size / 12);
    tft.drawFastHLine(cx - outer, cy, outer - inner, amber);
    tft.drawFastHLine(cx + inner + 1, cy, outer - inner, amber);
    tft.drawFastVLine(cx, cy - outer, outer - inner, amber);
    tft.drawFastVLine(cx, cy + inner + 1, outer - inner, amber);
}

void canopy(int x, int y, int size) {
    const uint16_t fill = getMutedTextColor();
    const int left = x + unit(3, size);
    const int top = y + unit(15, size);
    const int width = unit(26, size);
    const int height = max(5, unit(11, size));
    ui::fillSquircle(left, top, width, height, fill);
    tft.fillTriangle(x + unit(8, size), top + 2,
                     x + unit(16, size), y + unit(8, size),
                     x + unit(22, size), top + 2, fill);
    tft.drawFastHLine(left + 3, top + height - 1, width - 6, getSurfaceColor());
}

void weather(int code, int x, int y, int size) {
    const Sky mark = skyFor(code);
    if (mark == Sky::Sun) { sun(x, y, size, false); return; }
    if (mark == Sky::Haze) sun(x, y, size, true);
    if (mark == Sky::Fog) {
        const uint16_t ink = getMutedTextColor();
        const int thick = max(1, size / 16);
        tft.fillRoundRect(x + unit(3, size), y + unit(11, size), unit(22, size), thick,
                          thick / 2, ink);
        tft.fillRoundRect(x + unit(8, size), y + unit(19, size), unit(21, size), thick,
                          thick / 2, ink);
        return;
    }
    canopy(x, y, size);
    const int stroke = max(1, size / 16);
    if (mark == Sky::Rain) {
        for (int i = 0; i < 3; ++i) {
            const int px = x + unit(9 + i * 7, size);
            const int py = y + unit(27, size);
            for (int w = 0; w < stroke; ++w)
                tft.drawLine(px + w, py, px + w - unit(2, size), py + unit(4, size), getAccentColor());
        }
    } else if (mark == Sky::Snow) {
        const uint16_t ink = getTextColor();
        for (int i = 0; i < 2; ++i) {
            const int px = x + unit(11 + i * 11, size), py = y + unit(29, size);
            const int arm = max(2, unit(3, size));
            tft.drawFastHLine(px - arm, py, arm * 2 + 1, ink);
            tft.drawFastVLine(px, py - arm, arm * 2 + 1, ink);
        }
    } else if (mark == Sky::Thunder) {
        const uint16_t ink = getWarningColor();
        const int x0 = x + unit(19, size), y0 = y + unit(25, size);
        tft.drawLine(x0, y0, x0 - unit(5, size), y0 + unit(4, size), ink);
        tft.drawLine(x0 - unit(5, size), y0 + unit(4, size), x0 + unit(1, size), y0 + unit(4, size), ink);
        tft.drawLine(x0 + unit(1, size), y0 + unit(4, size), x0 - unit(3, size), y0 + unit(6, size), ink);
    }
}

void tinyPixel(int x, int y, int scale) {
    tft.fillRect(x, y, scale, scale, TFT_WHITE);
}

void busWord(int cx, int cy) {
    const int scale = 2, left = cx - 11, top = cy - 5;
    const uint8_t letters[3][5] = {
        {0b110, 0b101, 0b110, 0b101, 0b110},
        {0b000, 0b101, 0b101, 0b101, 0b111},
        {0b000, 0b011, 0b100, 0b001, 0b110}
    };
    for (int letter = 0; letter < 3; ++letter)
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (letters[letter][row] & (1 << (2 - col)))
                    tinyPixel(left + (letter * 4 + col) * scale, top + row * scale, scale);
}

void tramWord(int cx, int cy) {
    const int left = cx - 9, top = cy - 2;
    const uint8_t letters[4][5] = {
        {0b1111, 0b0100, 0b0100, 0b0100, 0b0100},
        {0b0000, 0b1101, 0b1010, 0b1000, 0b1000},
        {0b0000, 0b0110, 0b1001, 0b1111, 0b1001},
        {0b0000, 0b1010, 0b1101, 0b1001, 0b1001}
    };
    for (int letter = 0; letter < 4; ++letter)
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 4; ++col)
                if (letters[letter][row] & (1 << (3 - col)))
                    tinyPixel(left + letter * 5 + col, top + row, 1);
}

void diamond(int cx, int cy, int radius, uint16_t fill) {
    tft.fillTriangle(cx, cy - radius, cx + radius, cy, cx, cy + radius, fill);
    tft.fillTriangle(cx, cy - radius, cx - radius, cy, cx, cy + radius, fill);
}
}

void drawWeatherIconVector(int code, int x, int y) { weather(code, x, y, 32); }
void drawWeatherIconVectorSmall(int code, int x, int y) { weather(code, x, y, 24); }

void drawMoonPhaseIcon(int cx, int cy, int radius, int phase,
                       uint16_t outline, uint16_t background) {
    if (radius < 3) return;
    const int step = (phase % 8 + 8) % 8;
    tft.fillCircle(cx, cy, radius, background);
    const int inner = radius - 2;
    const uint16_t shade = getSurfaceColor();
    const uint16_t light = getWarningColor();
    for (int dy = -inner; dy <= inner; ++dy) {
        for (int dx = -inner; dx <= inner; ++dx) {
            if (dx * dx + dy * dy > inner * inner) continue;
            const bool illuminated = step == 4 ||
                (step == 1 && dx > inner / 2) ||
                (step == 2 && dx >= 0) ||
                (step == 3 && dx >= -inner / 2) ||
                (step == 5 && dx <= inner / 2) ||
                (step == 6 && dx <= 0) ||
                (step == 7 && dx < -inner / 2);
            tft.drawPixel(cx + dx, cy + dy, illuminated ? light : shade);
        }
    }
    tft.drawCircle(cx, cy, radius, outline);
}

void drawPageButton(PageButtonIcon symbol) {
    constexpr UiRect box = UiLayout::PAGE_NEXT;
    constexpr int cx = box.x + box.w / 2;
    constexpr int cy = box.y + box.h / 2;
    constexpr UiRect face{box.x + 1, box.y + 1, box.w - 1, box.h - 1};
    static_assert(face.x + face.w / 2 == cx && face.y + face.h / 2 == cy,
                  "Page button and pictogram must share one pixel center");
    constexpr UiRect artwork{cx - 11, cy - 11, 23, 24};
    static_assert(artwork.x >= box.x + 4 && artwork.right() <= box.right() - 4 &&
                  artwork.y >= box.y + 4 && artwork.bottom() <= box.bottom() - 4,
                  "Page artwork must clear the rounded button edge");
    const uint16_t ink = getOnAccentColor();
    ui::fillSquircle(face, getAccentColor());
    if (symbol == PageButtonIcon::Transit) {
        tft.fillRoundRect(cx - 10, cy - 11, 21, 21, 4, ink);
        tft.fillRoundRect(cx - 7, cy - 8, 15, 8, 2, getAccentColor());
        tft.fillCircle(cx - 6, cy + 5, 2, getAccentColor());
        tft.fillCircle(cx + 6, cy + 5, 2, getAccentColor());
        tft.fillRoundRect(cx - 8, cy + 10, 4, 3, 1, ink);
        tft.fillRoundRect(cx + 5, cy + 10, 4, 3, 1, ink);
    } else if (symbol == PageButtonIcon::HomeAssistant) {
        tft.fillRect(cx - 9, cy - 9, 2, 19, ink);
        tft.fillRect(cx - 3, cy - 9, 2, 19, ink);
        tft.fillRect(cx - 7, cy - 1, 4, 2, ink);
        tft.drawLine(cx + 2, cy + 9, cx + 5, cy - 9, ink);
        tft.drawLine(cx + 3, cy + 9, cx + 6, cy - 9, ink);
        tft.drawLine(cx + 5, cy - 9, cx + 9, cy + 9, ink);
        tft.drawLine(cx + 6, cy - 9, cx + 8, cy + 9, ink);
        tft.drawFastHLine(cx + 3, cy + 2, 6, ink);
        tft.drawFastHLine(cx + 3, cy + 3, 6, ink);
    } else {
        tft.fillCircle(cx, cy, 11, ink);
        tft.fillCircle(cx, cy, 8, getAccentColor());
        tft.fillRoundRect(cx - 1, cy - 6, 3, 7, 1, ink);
        tft.fillRoundRect(cx, cy - 1, 6, 3, 1, ink);
        tft.fillCircle(cx, cy, 2, ink);
    }
}

void drawSettingsIcon(uint16_t accent) {
    const UiRect hit = UiLayout::SETTINGS_BUTTON;
    const int x = hit.x + 3, y = hit.y + 2;
    ui::fillSquircle(x, y, hit.w - 6, hit.h - 4, getSurfaceColor());
    for (int i = 0; i < 3; ++i) {
        const int yy = y + 10 + i * 8;
        tft.drawFastHLine(x + 9, yy, hit.w - 24, getMutedTextColor());
        const int knobX = x + (i == 1 ? 25 : 15);
        tft.fillCircle(knobX, yy, 3, accent);
    }
}

void drawTransitProductIcon(int x, int y, uint8_t stored, int size) {
    if (size < 16) return;
    const int cx = x + size / 2, cy = y + size / 2;
    const transit::Product mode = transit::productFromStored(stored);
    uint16_t fill = getRaisedColor();
    const char *word = "?";
    bool circle = false, rotated = false, custom = false;
    int fontScale = 2;
    switch (mode) {
        case transit::Product::Sbahn: fill = S_GREEN; word = "S"; circle = true; fontScale = 3; break;
        case transit::Product::Ubahn: fill = U_BLUE; word = "U"; fontScale = 3; break;
        case transit::Product::Tram: fill = TRAM_RED; rotated = true; custom = true; break;
        case transit::Product::Bus: fill = BUS_PURPLE; circle = true; custom = true; break;
        case transit::Product::Ferry: fill = FERRY_CYAN; word = "F"; circle = true; fontScale = 3; break;
        case transit::Product::Regional: word = "RE"; break;
        case transit::Product::Express: word = "IC"; break;
        default: fontScale = 3; break;
    }
    if (circle) tft.fillCircle(cx, cy, size / 2, fill);
    else if (rotated) diamond(cx, cy, size / 2, fill);
    else ui::fillSquircle(x + 1, y + 1, size - 2, size - 2, fill);
    if (custom) {
        if (mode == transit::Product::Bus) busWord(cx, cy);
        else tramWord(cx, cy);
        return;
    }
    tft.setTextFont(1);
    tft.setTextSize(fontScale);
    tft.setTextDatum(MC_DATUM);
    if (mode == transit::Product::Sbahn || mode == transit::Product::Ferry) {
        tft.setTextColor(rgb(10, 45, 64));
        tft.drawString(word, cx - 1, cy, 1);
        tft.drawString(word, cx + 1, cy, 1);
    }
    tft.setTextColor(TFT_WHITE);
    tft.drawString(word, cx, cy, 1);
    tft.setTextSize(1);
    tft.setTextColor(getTextColor());
    tft.setTextDatum(TL_DATUM);
}
