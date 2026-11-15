// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

enum class AppPage : uint8_t { ClockWeather, Transport, HomeAssistant };

enum class PageButtonIcon : uint8_t { Transit, HomeAssistant, Clock };

extern AppPage currentPage;
