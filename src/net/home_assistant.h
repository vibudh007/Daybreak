// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

namespace homeassistant {

constexpr uint16_t MAX_ENTITIES = 256;

struct Entity {
    String id;
    String name;
    String state;
    bool selected = false;
};

struct State {
    String baseUrl;
    String username;
    String password;
    String accessToken;
    String refreshToken;
    String selectedIds;
    String error;
    Entity entities[MAX_ENTITIES];
    uint16_t count = 0;
    uint16_t selectedCount = 0;
    bool loaded = false;
    unsigned long accessExpiryMs = 0;
};

// The final 2x2 grid starts on a row boundary and may have one empty tile.
inline uint16_t selectedGridMaxOffset(uint16_t selectedCount) {
    return selectedCount > 4 ? ((selectedCount - 3) / 2) * 2 : 0;
}

extern State state;

void loadConfig(const String &baseUrl, const String &username, const String &password,
                const String &refreshToken, const String &selectedIds);
String normalizedBaseUrl(const String &baseUrl);
bool configured();
bool authenticate();
bool refreshEntities();
bool toggleEntity(uint16_t index);
void saveSelection();
void restoreSavedSelection();

}
