// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

extern bool isLightTheme;

namespace {
constexpr uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t((r & 0xf8) << 8) | uint16_t((g & 0xfc) << 3) | uint16_t(b >> 3);
}

constexpr ThemePalette NIGHT = {
    TFT_BLACK, color(21, 29, 42), color(39, 52, 72),
    color(244, 246, 250), color(182, 194, 210),
    color(38, 126, 237), TFT_WHITE,
    color(54, 179, 126), color(255, 181, 71), color(228, 88, 88),
    color(255, 181, 71)
};

constexpr ThemePalette DAY = {
    TFT_WHITE, color(240, 244, 249), color(226, 234, 244),
    color(20, 32, 51), color(82, 100, 122),
    color(23, 104, 197), TFT_WHITE,
    color(18, 118, 75), color(165, 98, 0), color(190, 43, 53),
    color(220, 96, 56)
};
}

const ThemePalette &activeTheme() { return isLightTheme ? DAY : NIGHT; }
uint16_t getBgColor() { return activeTheme().background; }
uint16_t getTextColor() { return activeTheme().text; }
uint16_t getSecHandColor() { return activeTheme().clockAccent; }
uint16_t getAccentColor() { return activeTheme().accent; }
uint16_t getOnAccentColor() { return activeTheme().onAccent; }
uint16_t getSurfaceColor() { return activeTheme().surface; }
uint16_t getRaisedColor() { return activeTheme().raised; }
uint16_t getMutedTextColor() { return activeTheme().mutedText; }
uint16_t getSuccessColor() { return activeTheme().success; }
uint16_t getWarningColor() { return activeTheme().warning; }
uint16_t getDangerColor() { return activeTheme().danger; }
