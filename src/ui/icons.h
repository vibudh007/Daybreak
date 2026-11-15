// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include "pages.h"

// Weather and transport positions are top-left; moon positioning uses its center.
void drawWeatherIconVector(int code, int x, int y);
void drawWeatherIconVectorSmall(int code, int x, int y);
void drawMoonPhaseIcon(int centerX, int centerY, int radius, int phase,
                       uint16_t outline, uint16_t background);
void drawPageButton(PageButtonIcon icon);
void drawSettingsIcon(uint16_t accent);
void drawTransitProductIcon(int x, int y, uint8_t product, int size = 28);
