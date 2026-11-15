// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace preferences_util {

inline bool putString(Preferences &store, const char *key, const String &value) {
    bool ok = store.putString(key, value) == value.length();
    if (value.isEmpty()) ok = ok && store.isKey(key) && store.getString(key) == value;
    if (!ok) log_e("[NVS] Could not save string setting: %s", key);
    return ok;
}

inline bool putInt(Preferences &store, const char *key, int value) {
    const bool ok = store.putInt(key, value) == sizeof(int32_t);
    if (!ok) log_e("[NVS] Could not save integer setting: %s", key);
    return ok;
}

inline bool putFloat(Preferences &store, const char *key, float value) {
    const bool ok = store.putFloat(key, value) == sizeof(float);
    if (!ok) log_e("[NVS] Could not save numeric setting: %s", key);
    return ok;
}

inline bool putBool(Preferences &store, const char *key, bool value) {
    const bool ok = store.putBool(key, value) == sizeof(uint8_t);
    if (!ok) log_e("[NVS] Could not save boolean setting: %s", key);
    return ok;
}

inline bool putUChar(Preferences &store, const char *key, uint8_t value) {
    const bool ok = store.putUChar(key, value) == sizeof(uint8_t);
    if (!ok) log_e("[NVS] Could not save byte setting: %s", key);
    return ok;
}

}
