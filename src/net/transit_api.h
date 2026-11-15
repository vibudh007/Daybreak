// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

namespace transit {

constexpr uint8_t MAX_MATCHES = 8;
constexpr uint8_t MAX_SERVICES = 16;
constexpr uint8_t MAX_CANDIDATES = 32;

enum class Product : uint8_t { Sbahn, Ubahn, Tram, Bus, Ferry, Regional, Express, Unknown };

struct Stop {
    String id;
    String name;
    String host;
    Product product = Product::Unknown;
};

struct Service {
    String tripId;
    String stopId;
    String line;
    String lineId;
    String standardDirection;
    String direction;
    String when;
    String platform;
    String remark;
    bool hasDelay = false;
    bool useHeadsign = false;
    bool hasTravelBearing = false;
    Product product = Product::Unknown;
    int delaySeconds = 0;
    int64_t epoch = 0;
    float stopLatitude = 0;
    float stopLongitude = 0;
    float destinationLatitude = 0;
    float destinationLongitude = 0;
    bool hasDirectionCoordinates = false;
    float travelX = 0;
    float travelY = 0;
    bool cancelled = false;
};

struct State {
    Stop savedStop;
    String query;
    String error;
    Stop matches[MAX_MATCHES];
    uint8_t matchCount = 0;
    int8_t selectedMatch = -1;
    Service directionA[MAX_SERVICES / 2];
    Service directionB[MAX_SERVICES / 2];
    uint8_t directionACount = 0;
    uint8_t directionBCount = 0;
    uint8_t unverifiedCount = 0;
    bool directionsPending = false;
    bool boardLoaded = false;
};

extern State state;

bool searchStops(const String &query);
bool refreshBoard();
void loadSavedStop(const String &id, const String &name, const String &host, uint8_t product);
void saveSelectedStop();
String encodeQuery(const String &value);
const char *productLabel(Product product);
Product productFromStored(uint8_t product);
uint8_t productToStored(Product product);

}
