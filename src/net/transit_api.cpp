// SPDX-License-Identifier: GPL-3.0-or-later
#include "transit_api.h"
#include "../util/iso8601_epoch.h"
#include "trusted_ca.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <StreamString.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <math.h>
#include <time.h>
#include <string.h>

namespace transit {

State state;

namespace {
constexpr char VBB_HOST[] = "v6.vbb.transport.rest";
constexpr char BVG_HOST[] = "v6.bvg.transport.rest";
constexpr unsigned long REQUEST_TIMEOUT_MS = 7000;

// Network refresh runs on a small task stack, so service scratch space is static.
Service departureScratch[MAX_SERVICES];
Service directionAScratch[MAX_SERVICES / 2];
Service directionBScratch[MAX_SERVICES / 2];
Service mergeScratch[MAX_SERVICES];

// HTTPClient decodes both chunked and length-framed responses into this bounded sink.
class BoundedResponse : public StreamString {
public:
    explicit BoundedResponse(size_t limit) : limit_(limit) {}

    size_t write(const uint8_t *data, size_t size) override {
        if (size > limit_ - length()) { oversized_ = true; return 0; }
        return StreamString::write(data, size);
    }
    size_t write(uint8_t value) override { return write(&value, 1); }
    bool oversized() const { return oversized_; }

private:
    size_t limit_;
    bool oversized_ = false;
};

String selectedHost() {
    if (state.savedStop.host == BVG_HOST) return state.savedStop.host;
    return VBB_HOST;
}

bool getJson(const String &host, const String &path, JsonDocument &doc,
             JsonDocument *filter = nullptr) {
    if (WiFi.status() != WL_CONNECTED) {
        state.error = "Wi-Fi is not connected";
        return false;
    }

    WiFiClientSecure client;
    if (!network::configureTrustedCaBundle(client)) {
        state.error = "Trusted HTTPS certificate bundle unavailable";
        return false;
    }
    client.setHandshakeTimeout(7);
    HTTPClient http;
    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.useHTTP10(true);
    const String url = "https://" + host + path;
    if (!http.begin(client, url)) {
        state.error = "Could not start HTTPS request";
        return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        if (status > 0) {
            state.error = "Transport API returned HTTP " + String(status);
        } else {
            state.error = String("Transport API connection failed: ") + http.errorToString(status);
            char tlsError[128] = {};
            client.lastError(tlsError, sizeof(tlsError));
            log_w("[TRANSIT] Request to %s failed (%s; TLS: %s)", host.c_str(),
                  http.errorToString(status).c_str(), tlsError);
        }
        http.end();
        return false;
    }

    // Leave heap space for the filtered JSON document and TLS client.
    const size_t bodyLimit = std::min<size_t>(96 * 1024, ESP.getFreeHeap() / 2);
    const int contentLength = http.getSize();
    if (bodyLimit < 4096 || (contentLength > 0 && static_cast<size_t>(contentLength) > bodyLimit)) {
        http.end();
        state.error = "Transport API response exceeds available memory";
        return false;
    }
    BoundedResponse response(bodyLimit);
    if (contentLength > 0 && !response.reserve(contentLength)) {
        http.end();
        state.error = "Could not allocate transport API response";
        return false;
    }
    const int received = http.writeToStream(&response);
    if (received < 0 || response.oversized()) {
        http.end();
        state.error = response.oversized() ? "Transport API response exceeds available memory" :
                     "Could not read transport API response";
        return false;
    }
    const String &payload = response;
    if (!payload.length()) {
        http.end();
        state.error = "Transport API returned an empty response";
        log_w("[TRANSIT] Empty body from %s%s (HTTP %d)",
              host.c_str(), path.c_str(), status);
        return false;
    }

    const DeserializationError parseError = filter
        ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
        : deserializeJson(doc, payload);
    if (parseError) {
        http.end();
        state.error = String("Could not parse transport API response: ") + parseError.c_str();
        log_w("[TRANSIT] JSON parse failed (%s, %u bytes) from %s%s",
              parseError.c_str(), static_cast<unsigned>(payload.length()), host.c_str(), path.c_str());
        return false;
    }
    http.end();
    return true;
}

Product parseProduct(const char *product) {
    if (!product) return Product::Unknown;
    if (!strcmp(product, "suburban")) return Product::Sbahn;
    if (!strcmp(product, "subway")) return Product::Ubahn;
    if (!strcmp(product, "tram")) return Product::Tram;
    if (!strcmp(product, "bus")) return Product::Bus;
    if (!strcmp(product, "ferry")) return Product::Ferry;
    if (!strcmp(product, "regional")) return Product::Regional;
    if (!strcmp(product, "express")) return Product::Express;
    return Product::Unknown;
}

Product primaryStopProduct(JsonObjectConst products) {
    if (products["suburban"] | false) return Product::Sbahn;
    if (products["subway"] | false) return Product::Ubahn;
    if (products["tram"] | false) return Product::Tram;
    if (products["bus"] | false) return Product::Bus;
    if (products["ferry"] | false) return Product::Ferry;
    if (products["regional"] | false) return Product::Regional;
    if (products["express"] | false) return Product::Express;
    return Product::Unknown;
}

String compactRemark(JsonArrayConst remarks, bool &cancelled) {
    for (JsonObjectConst item : remarks) {
        const String type = item["type"] | "";
        const String code = item["code"] | "";
        const String summary = item["summary"] | "";
        const String text = item["text"] | "";
        if (type == "hint" || code == "OPERATOR" || code.startsWith("text.occup.")) continue;
        String message = summary;
        if (text.length()) {
            if (message.length()) message += " ";
            message += text;
        }
        if (!message.length()) continue;
        String lower = message;
        lower.toLowerCase();
        if (lower.indexOf("cancelled stop") >= 0 || lower.indexOf("stop cancelled") >= 0 ||
            lower.indexOf("halt entfällt") >= 0 || lower.indexOf("halt entfaellt") >= 0) return "Stop skipped";
        if (lower.indexOf("police") >= 0 || lower.indexOf("polizeieinsatz") >= 0 ||
            lower.indexOf("polizeilicher einsatz") >= 0 || lower.indexOf("einsatz der polizei") >= 0) return "Police work";
        if (lower.indexOf("medical emergency") >= 0 || lower.indexOf("medical incident") >= 0 ||
            lower.indexOf("medical assistance") >= 0 || lower.indexOf("medizinischer notfall") >= 0 ||
            lower.indexOf("rettungseinsatz") >= 0 || lower.indexOf("notarzteinsatz") >= 0 ||
            lower.indexOf("krankheitsfall") >= 0) return "Medical emergency";
        if (lower.indexOf("construction") >= 0 || lower.indexOf("road works") >= 0 ||
            lower.indexOf("track works") >= 0 || lower.indexOf("bauarbeiten") >= 0 ||
            lower.indexOf("gleisbau") >= 0 || lower.indexOf("baustelle") >= 0) return "Construction work";
        if (lower.indexOf("fire brigade") >= 0 || lower.indexOf("fire service") >= 0 ||
            lower.indexOf("feuerwehr") >= 0 || lower.indexOf("brand") >= 0) return "Fire service";
        if (lower.indexOf("accident") >= 0 || lower.indexOf("verkehrsunfall") >= 0 ||
            lower.indexOf("unfall") >= 0) return "Accident";
        if (lower.indexOf("blocked track") >= 0 || lower.indexOf("track obstruction") >= 0 ||
            lower.indexOf("gegenstand im gleis") >= 0 || lower.indexOf("streckensperrung") >= 0) return "Route blocked";
        if (lower.indexOf("strike") >= 0 || lower.indexOf("streik") >= 0) return "Strike";
        if (lower.indexOf("signal fault") >= 0 || lower.indexOf("signal failure") >= 0 ||
            lower.indexOf("signalstörung") >= 0 || lower.indexOf("signalstoerung") >= 0 ||
            lower.indexOf("technical fault") >= 0 || lower.indexOf("technische störung") >= 0 ||
            lower.indexOf("technische stoerung") >= 0) return "Technical fault";
        if (lower.indexOf("severe weather") >= 0 || lower.indexOf("storm") >= 0 ||
            lower.indexOf("unwetter") >= 0 || lower.indexOf("sturm") >= 0) return "Severe weather";
        if (lower.indexOf("demonstration") >= 0 || lower.indexOf("kundgebung") >= 0) return "Demonstration";
        if (lower.indexOf("diversion") >= 0 || lower.indexOf("detour") >= 0 ||
            lower.indexOf("umleitung") >= 0) return "Detour";
        if (lower.indexOf("replacement service") >= 0 || lower.indexOf("ersatzverkehr") >= 0) return "Replacement service";
        if (lower.indexOf("cancel") >= 0 || lower.indexOf("ausfall") >= 0 ||
            lower.indexOf("entfällt") >= 0 || lower.indexOf("entfaellt") >= 0) {
            cancelled = true;
            return "Cancelled";
        }
        if (summary.length() && summary.length() <= 18) return summary;
        return "Service notice";
    }
    return "";
}

bool parseServices(JsonDocument &doc, const char *envelope, Service output[], uint8_t &count) {
    JsonArrayConst items;
    if (doc.is<JsonArrayConst>()) items = doc.as<JsonArrayConst>();
    else items = doc[envelope].as<JsonArrayConst>();
    if (items.isNull()) {
        state.error = "Transport API response has no service list";
        return false;
    }

    count = 0;
    for (JsonObjectConst item : items) {
        if (count >= MAX_SERVICES) break;
        Service &service = output[count];
        service.tripId = item["tripId"] | "";
        service.line = item["line"]["name"] | "?";
        service.product = parseProduct(item["line"]["product"] | "");
        service.lineId = String(static_cast<uint8_t>(service.product)) + ":" + service.line;
        service.standardDirection = item["direction"] | "";
        service.direction = item["destination"]["name"] | "";
        if (!service.direction.length()) service.direction = service.standardDirection;
        if (!service.direction.length()) service.direction = "Destination unavailable";
        const JsonObjectConst stopLocation = item["stop"]["location"].as<JsonObjectConst>();
        const JsonObjectConst destinationLocation = item["destination"]["location"].as<JsonObjectConst>();
        service.stopLatitude = stopLocation["latitude"] | 0.0f;
        service.stopLongitude = stopLocation["longitude"] | 0.0f;
        service.destinationLatitude = destinationLocation["latitude"] | 0.0f;
        service.destinationLongitude = destinationLocation["longitude"] | 0.0f;
        service.hasDirectionCoordinates = !stopLocation.isNull() && !destinationLocation.isNull() &&
            (service.stopLatitude != service.destinationLatitude ||
             service.stopLongitude != service.destinationLongitude);
        service.when = item["when"] | "";
        if (!service.when.length()) service.when = item["plannedWhen"] | "";
        service.platform = item["platform"] | "";
        const JsonVariantConst delay = item["delay"];
        service.delaySeconds = delay | 0;
        const String plannedWhen = item["plannedWhen"] | "";
        service.onTime = (!delay.isNull() && service.delaySeconds == 0) ||
                         (service.when.length() && plannedWhen.length() && service.when == plannedWhen);
        service.epoch = util::parseIsoEpoch(service.when.c_str(), service.when.length());
        if (!service.epoch) continue;
        service.remark = "";
        service.cancelled = item["cancelled"] | false;
        service.remark = compactRemark(item["remarks"].as<JsonArrayConst>(), service.cancelled);
        ++count;
    }

    std::sort(output, output + count, [](const Service &a, const Service &b) {
        if (!a.epoch) return false;
        if (!b.epoch) return true;
        return a.epoch < b.epoch;
    });
    return true;
}

bool fetchServiceList(const String &path, const char *envelope, Service output[], uint8_t &count) {
    JsonDocument doc;
    JsonDocument filter;
    JsonArray services = filter[envelope].to<JsonArray>();
    JsonObject item = services.add<JsonObject>();
    item["tripId"] = true;
    item["line"]["name"] = true;
    item["line"]["product"] = true;
    item["direction"] = true;
    item["destination"]["name"] = true;
    item["destination"]["location"]["latitude"] = true;
    item["destination"]["location"]["longitude"] = true;
    item["stop"]["location"]["latitude"] = true;
    item["stop"]["location"]["longitude"] = true;
    item["when"] = true;
    item["plannedWhen"] = true;
    item["platform"] = true;
    item["delay"] = true;
    item["cancelled"] = true;
    JsonArray remarks = item["remarks"].to<JsonArray>();
    JsonObject remark = remarks.add<JsonObject>();
    remark["type"] = true;
    remark["code"] = true;
    remark["summary"] = true;
    remark["text"] = true;
    if (!getJson(selectedHost(), path, doc, &filter)) return false;
    return parseServices(doc, envelope, output, count);
}

bool directionVector(const Service &service, float &x, float &y) {
    if (!service.hasDirectionCoordinates) return false;
    constexpr float TRANSIT_DEG_TO_RAD = 0.01745329252f;
    const float meanLatitude = (service.stopLatitude + service.destinationLatitude) * 0.5f;
    x = (service.destinationLongitude - service.stopLongitude) * cosf(meanLatitude * TRANSIT_DEG_TO_RAD);
    y = service.destinationLatitude - service.stopLatitude;
    const float length = sqrtf(x * x + y * y);
    if (length < 0.00001f) return false;
    x /= length;
    y /= length;
    return true;
}

RouteDirections *findRoute(const String &lineId, bool create) {
    for (uint8_t i = 0; i < state.routeDirectionCount; ++i)
        if (state.routeDirections[i].lineId == lineId) return &state.routeDirections[i];
    if (!create || state.routeDirectionCount >= MAX_ROUTE_DIRECTIONS) return nullptr;
    RouteDirections &route = state.routeDirections[state.routeDirectionCount++];
    // Reused slots must not retain the previous stop's direction axis.
    route = RouteDirections{};
    route.lineId = lineId;
    return &route;
}

void prepareRouteAxes(Service services[], uint8_t count) {
    // The most opposed terminal pair defines a stable split for branched routes.
    for (uint8_t i = 0; i < count; ++i) {
        if (!services[i].lineId.length()) continue;
        RouteDirections *route = findRoute(services[i].lineId, true);
        if (!route || route->axisReady) continue;

        float bestDot = 1.0f;
        float sideAX = 0, sideAY = 0, sideBX = 0, sideBY = 0;
        String sideADirection, sideBDirection;
        for (uint8_t a = 0; a < count; ++a) {
            if (services[a].lineId != route->lineId || !services[a].standardDirection.length()) continue;
            float ax, ay;
            if (!directionVector(services[a], ax, ay)) continue;
            for (uint8_t b = a + 1; b < count; ++b) {
                if (services[b].lineId != route->lineId ||
                    services[b].standardDirection == services[a].standardDirection) continue;
                float bx, by;
                if (!directionVector(services[b], bx, by)) continue;
                const float dot = ax * bx + ay * by;
                if (dot < bestDot) {
                    bestDot = dot;
                    if (services[a].standardDirection < services[b].standardDirection) {
                        sideADirection = services[a].standardDirection;
                        sideBDirection = services[b].standardDirection;
                        sideAX = ax; sideAY = ay; sideBX = bx; sideBY = by;
                    } else {
                        sideADirection = services[b].standardDirection;
                        sideBDirection = services[a].standardDirection;
                        sideAX = bx; sideAY = by; sideBX = ax; sideBY = ay;
                    }
                }
            }
        }
        if (bestDot >= 0.25f || !sideADirection.length() || !sideBDirection.length()) continue;
        route->sideADirection = sideADirection;
        route->sideBDirection = sideBDirection;
        route->axisX = sideAX - sideBX;
        route->axisY = sideAY - sideBY;
        const float axisLength = sqrtf(route->axisX * route->axisX + route->axisY * route->axisY);
        if (axisLength < 0.00001f) continue;
        route->axisX /= axisLength;
        route->axisY /= axisLength;
        route->axisReady = true;
    }
}

uint8_t routeSide(const Service &service) {
    RouteDirections *route = findRoute(service.lineId, false);
    if (!route || !route->axisReady) return 2;
    if (service.standardDirection == route->sideADirection) return 0;
    if (service.standardDirection == route->sideBDirection) return 1;

    float x, y;
    if (!directionVector(service, x, y)) return 2;
    const float projection = x * route->axisX + y * route->axisY;
    if (fabsf(projection) < 0.08f) return 2;
    return projection > 0 ? 0 : 1;
}

bool belongsToRouteSide(const Service &service, uint8_t side) {
    return routeSide(service) == side;
}

void mergeUpcomingServices(Service existing[], uint8_t &existingCount,
                           Service updates[], uint8_t updateCount, uint8_t side, time_t now) {
    // Keep still-upcoming services when a limited API window omits one side.
    Service *merged = mergeScratch;
    uint8_t mergedCount = 0;
    auto sameTrip = [](const Service &a, const Service &b) {
        if (a.tripId.length() && b.tripId.length()) return a.tripId == b.tripId;
        return a.line == b.line && a.when == b.when && a.standardDirection == b.standardDirection;
    };

    for (uint8_t i = 0; i < existingCount && mergedCount < MAX_SERVICES; ++i) {
        if (!belongsToRouteSide(existing[i], side)) continue;
        if (existing[i].epoch && now >= 1700000000 && existing[i].epoch <= static_cast<int64_t>(now)) continue;
        merged[mergedCount++] = existing[i];
    }
    for (uint8_t i = 0; i < updateCount; ++i) {
        int match = -1;
        for (uint8_t j = 0; j < mergedCount; ++j) {
            if (sameTrip(merged[j], updates[i])) { match = j; break; }
        }
        if (match >= 0) merged[match] = updates[i];
        else if (mergedCount < MAX_SERVICES) merged[mergedCount++] = updates[i];
    }
    std::sort(merged, merged + mergedCount, [](const Service &a, const Service &b) {
        if (!a.epoch) return false;
        if (!b.epoch) return true;
        return a.epoch < b.epoch;
    });

    existingCount = 0;
    for (uint8_t i = 0; i < mergedCount && existingCount < MAX_SERVICES / 2; ++i)
        existing[existingCount++] = merged[i];
}

}

String encodeQuery(const String &value) {
    static const char hex[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(value[i]);
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%';
            encoded += hex[ch >> 4];
            encoded += hex[ch & 0x0F];
        }
    }
    return encoded;
}

bool searchStops(const String &query) {
    state.error = "";
    state.matchCount = 0;
    state.selectedMatch = -1;
    if (!query.length()) {
        state.error = "Enter a stop name";
        return false;
    }

    const char *hosts[] = {VBB_HOST, BVG_HOST};
    for (const char *host : hosts) {
        JsonDocument doc;
        const String path = "/locations?query=" + encodeQuery(query) +
            "&stops=true&addresses=false&poi=false&linesOfStops=false&results=8&language=en&pretty=false";
        if (!getJson(host, path, doc)) continue;
        state.error = "";

        JsonArrayConst results;
        if (doc.is<JsonArrayConst>()) results = doc.as<JsonArrayConst>();
        else results = doc["locations"].as<JsonArrayConst>();
        if (results.isNull()) {
            state.error = "Search response has no stop list";
            continue;
        }

        for (JsonObjectConst item : results) {
            if (state.matchCount >= MAX_MATCHES) break;
            if (strcmp(item["type"] | "", "stop") != 0) continue;
            const String id = item["id"] | "";
            const String name = item["name"] | "";
            if (!id.length() || !name.length()) continue;
            Stop &stop = state.matches[state.matchCount++];
            stop.id = id;
            stop.name = name;
            stop.host = host;
            stop.product = primaryStopProduct(item["products"].as<JsonObjectConst>());
        }
        if (state.matchCount) return true;
    }
    if (!state.error.length()) state.error = "No matching stops found";
    return false;
}

bool refreshBoard() {
    state.error = "";
    if (!state.savedStop.id.length()) {
        state.error = "Add a stop to see departures";
        return false;
    }

    uint8_t newDepartureCount = 0;
    const String base = "/stops/" + encodeQuery(state.savedStop.id);
    const String params = "?duration=90&results=16&remarks=true&linesOfStops=false&language=en&pretty=false";
    if (!fetchServiceList(base + "/departures" + params, "departures", departureScratch, newDepartureCount)) return false;

    prepareRouteAxes(departureScratch, newDepartureCount);
    // A one-sided API window must not replace the last complete board.
    bool hasClassifiableDirection = false;
    for (uint8_t i = 0; i < newDepartureCount; ++i)
        if (routeSide(departureScratch[i]) < 2) { hasClassifiableDirection = true; break; }
    if (newDepartureCount && !hasClassifiableDirection) {
        state.error = "Waiting for both route directions";
        return false;
    }
    uint8_t updatedACount = 0, updatedBCount = 0;
    uint8_t unclassifiedCount = 0;
    for (uint8_t i = 0; i < newDepartureCount; ++i) {
        Service &service = departureScratch[i];
        const uint8_t side = routeSide(service);
        if (side > 1) { ++unclassifiedCount; continue; }
        service.directionSide = side;
        if (side == 0 && updatedACount < MAX_SERVICES / 2) {
            directionAScratch[updatedACount++] = service;
        } else if (side == 1 && updatedBCount < MAX_SERVICES / 2) {
            directionBScratch[updatedBCount++] = service;
        }
    }
    const time_t now = time(nullptr);
    mergeUpcomingServices(state.directionA, state.directionACount, directionAScratch, updatedACount, 0, now);
    mergeUpcomingServices(state.directionB, state.directionBCount, directionBScratch, updatedBCount, 1, now);
    state.boardLoaded = true;
    state.error = unclassifiedCount ? "Some services omitted: direction unavailable" : "";
    return true;
}

void loadSavedStop(const String &id, const String &name, const String &host, uint8_t product) {
    state.savedStop.id = id;
    state.savedStop.name = name;
    state.savedStop.host = host.length() ? host : VBB_HOST;
    state.savedStop.product = productFromStored(product);
}

void saveSelectedStop() {
    if (state.selectedMatch < 0 || state.selectedMatch >= state.matchCount) return;
    state.savedStop = state.matches[state.selectedMatch];
    state.boardLoaded = false;
    state.directionACount = state.directionBCount = 0;
    state.routeDirectionCount = 0;
    state.error = "";
}

const char *productLabel(Product product) {
    switch (product) {
        case Product::Sbahn: return "S-Bahn";
        case Product::Ubahn: return "U-Bahn";
        case Product::Tram: return "Tram";
        case Product::Bus: return "Bus";
        case Product::Ferry: return "Ferry";
        case Product::Regional: return "Regional";
        case Product::Express: return "Express";
        default: return "Transit";
    }
}

Product productFromStored(uint8_t product) {
    return product <= static_cast<uint8_t>(Product::Unknown) ? static_cast<Product>(product) : Product::Unknown;
}

uint8_t productToStored(Product product) { return static_cast<uint8_t>(product); }

}
