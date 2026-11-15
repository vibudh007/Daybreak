// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

struct ThemePalette {
    uint16_t background;
    uint16_t surface;
    uint16_t raised;
    uint16_t text;
    uint16_t mutedText;
    uint16_t accent;
    uint16_t onAccent;
    uint16_t success;
    uint16_t warning;
    uint16_t danger;
    uint16_t clockAccent;
};

const ThemePalette &activeTheme();
uint16_t getAccentColor();
uint16_t getOnAccentColor();
uint16_t getSurfaceColor();
uint16_t getRaisedColor();
uint16_t getMutedTextColor();
uint16_t getSuccessColor();
uint16_t getWarningColor();
uint16_t getDangerColor();

uint16_t getBgColor();

uint16_t getTextColor();

uint16_t getSecHandColor();
