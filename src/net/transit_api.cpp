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
constexpr unsigned long BEARING_TTL_MS = 30UL * 60000UL;
constexpr unsigned long FAILED_RETRY_MS = 2UL * 60000UL;
constexpr unsigned long COMPACT_BOARD_TTL_MS = 30UL * 60000UL;
constexpr uint8_t MAX_TRIP_PROBES_PER_REFRESH = 8;

// Network refresh runs on a small task stack, so service scratch space is static.
Service departureScratch[MAX_CANDIDATES];
Service directionAScratch[MAX_SERVICES / 2];
Service directionBScratch[MAX_SERVICES / 2];

struct BearingCacheEntry {
    uint64_t key = 0;
    float x = 0;
    float y = 0;
    unsigned long checkedAt = 0;
    bool valid = false;
};
BearingCacheEntry bearingCache[MAX_CANDIDATES];
uint8_t bearingCacheCount = 0;
String bearingCacheStopId;
String boardRequestStopId;
unsigned long compactBoardUntil = 0;
bool compactBoardActive = false;

// HTTPClient decodes both chunked and length-framed responses into this bounded sink.
class BoundedResponse : public StreamString {
public:
    explicit BoundedResponse(size_t limit) : limit_(limit) {}

    size_t write(const uint8_t *data, size_t size) override {
        if (size > limit_ - length()) { oversized_ = true; return 0; }
        const size_t written = StreamString::write(data, size);
        if (written != size) allocationFailed_ = true;
        return written;
    }
    size_t write(uint8_t value) override { return write(&value, 1); }
    bool oversized() const { return oversized_ || allocationFailed_; }

private:
    size_t limit_;
    bool oversized_ = false;
    bool allocationFailed_ = false;
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
    const size_t freeHeap = ESP.getFreeHeap();
    const size_t bodyLimit = std::min<size_t>(96 * 1024, freeHeap * 3 / 4);
    const int contentLength = http.getSize();
    if (bodyLimit < 4096 || (contentLength > 0 && static_cast<size_t>(contentLength) > bodyLimit)) {
        log_w("[TRANSIT] Response too large: length=%d limit=%u free=%u",
              contentLength, static_cast<unsigned>(bodyLimit), static_cast<unsigned>(freeHeap));
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
    if (received < 0 || response.oversized() ||
        (contentLength >= 0 && received != contentLength)) {
        log_w("[TRANSIT] Response read failed: length=%d received=%d limit=%u free=%u",
              contentLength, received, static_cast<unsigned>(bodyLimit), static_cast<unsigned>(freeHeap));
        http.end();
        state.error = response.oversized() ? "Transport API response exceeds available memory" :
                     "Incomplete transport API response";
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

String compactRemark(JsonArrayConst remarks) {
    for (JsonObjectConst item : remarks) {
        const String type = item["type"] | "";
        const String code = item["code"] | "";
        const String summary = item["summary"] | "";
        const String text = item["text"] | "";
        if (type == "hint" || code == "OPERATOR" || code.startsWith("text.occup.")) continue;
        if (code.indexOf("partially.cancelled") >= 0) return "Some stops skipped";
        if (code.indexOf("platformChange") >= 0) return "Platform changed";
        String message = summary;
        if (text.length()) {
            if (message.length()) message += " ";
            message += text;
        }
        if (!message.length()) continue;
        String lower = message;
        lower.toLowerCase();
        if (lower.indexOf("intermediate stops") >= 0) return "Some stops skipped";
        if (lower.indexOf("cancelled stop") >= 0 || lower.indexOf("stop cancelled") >= 0 ||
            lower.indexOf("halt entfällt") >= 0 || lower.indexOf("halt entfaellt") >= 0) return "Stop skipped";
        if (lower.indexOf("police") >= 0 || lower.indexOf("polizeieinsatz") >= 0 ||
            lower.indexOf("polizeilicher einsatz") >= 0 || lower.indexOf("einsatz der polizei") >= 0) return "Police work";
        if (lower.indexOf("medical emergency") >= 0 || lower.indexOf("medical incident") >= 0 ||
            lower.indexOf("medical assistance") >= 0 || lower.indexOf("medizinischer notfall") >= 0 ||
            lower.indexOf("rettungseinsatz") >= 0 || lower.indexOf("notarzteinsatz") >= 0 ||
            lower.indexOf("krankheitsfall") >= 0) return "Medical emergency";
        if (lower.indexOf("replacement service") >= 0 || lower.indexOf("ersatzverkehr") >= 0) return "Replacement bus";
        if (lower.indexOf("diversion") >= 0 || lower.indexOf("detour") >= 0 ||
            lower.indexOf("umleitung") >= 0) return "Detour";
        if (lower.indexOf("moved stop") >= 0 || lower.indexOf("verlegte haltestelle") >= 0) return "Stop moved";
        if (lower.indexOf("elevator out of service") >= 0 || lower.indexOf("aufzug außer betrieb") >= 0) return "Lift unavailable";
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
        if (lower.indexOf("cancel") >= 0 || lower.indexOf("ausfall") >= 0 ||
            lower.indexOf("entfällt") >= 0 || lower.indexOf("entfaellt") >= 0) {
            return "Service change";
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
        if (count >= MAX_CANDIDATES) break;
        Service &service = output[count];
        service = Service{};
        service.tripId = item["tripId"] | "";
        service.stopId = item["stop"]["id"] | "";
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
        service.hasDelay = !delay.isNull();
        service.delaySeconds = delay | 0;
        service.epoch = util::parseIsoEpoch(service.when.c_str(), service.when.length());
        if (!service.epoch) continue;
        service.remark = "";
        service.cancelled = item["cancelled"] | false;
        service.remark = compactRemark(item["remarks"].as<JsonArrayConst>());
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
    item["stop"]["id"] = true;
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

bool normalizedVector(float fromLatitude, float fromLongitude, float toLatitude,
                      float toLongitude, float &x, float &y) {
    if (fabsf(fromLatitude) > 90 || fabsf(toLatitude) > 90 ||
        fabsf(fromLongitude) > 180 || fabsf(toLongitude) > 180 ||
        (fromLatitude == 0 && fromLongitude == 0) ||
        (toLatitude == 0 && toLongitude == 0)) return false;
    constexpr float TO_RADIANS = 0.01745329252f;
    x = (toLongitude - fromLongitude) * cosf((fromLatitude + toLatitude) * 0.5f * TO_RADIANS);
    y = toLatitude - fromLatitude;
    const float length = sqrtf(x * x + y * y);
    if (!isfinite(length) || length < 0.00001f) return false;
    x /= length;
    y /= length;
    return true;
}

bool terminalVector(const Service &service, float &x, float &y) {
    return service.hasDirectionCoordinates && normalizedVector(
        service.stopLatitude, service.stopLongitude, service.destinationLatitude,
        service.destinationLongitude, x, y);
}

uint64_t bearingKey(const Service &service) {
    uint64_t hash = 1469598103934665603ULL;
    auto add = [&hash](const String &value) {
        for (size_t i = 0; i < value.length(); ++i) {
            hash ^= static_cast<uint8_t>(value[i]);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xff;
        hash *= 1099511628211ULL;
    };
    add(service.lineId);
    add(service.stopId);
    add(service.standardDirection);
    add(service.direction);
    if (!service.standardDirection.length()) add(service.tripId);
    return hash;
}

BearingCacheEntry *cachedBearing(uint64_t key) {
    for (uint8_t i = 0; i < bearingCacheCount; ++i)
        if (bearingCache[i].key == key) return &bearingCache[i];
    return nullptr;
}

BearingCacheEntry &cacheSlot(uint64_t key) {
    if (BearingCacheEntry *existing = cachedBearing(key)) return *existing;
    uint8_t slot = bearingCacheCount;
    if (bearingCacheCount < MAX_CANDIDATES) ++bearingCacheCount;
    else {
        unsigned long oldest = 0;
        for (uint8_t i = 0; i < bearingCacheCount; ++i) {
            const unsigned long age = millis() - bearingCache[i].checkedAt;
            if (age >= oldest) { oldest = age; slot = i; }
        }
    }
    bearingCache[slot] = BearingCacheEntry{};
    bearingCache[slot].key = key;
    return bearingCache[slot];
}

bool tripBearing(const Service &service, float &x, float &y) {
    if (!service.tripId.length() || !service.stopId.length()) return false;
    JsonDocument doc, filter;
    JsonObject item = filter["trip"]["stopovers"].to<JsonArray>().add<JsonObject>();
    item["stop"]["id"] = true;
    item["stop"]["location"]["latitude"] = true;
    item["stop"]["location"]["longitude"] = true;
    item["departure"] = true;
    item["plannedDeparture"] = true;
    const String previousError = state.error;
    const bool fetched = getJson(selectedHost(), "/trips/" + encodeQuery(service.tripId) +
        "?stopovers=true&remarks=false&pretty=false", doc, &filter);
    if (!fetched) log_w("[TRANSIT] Trip direction unavailable for %s (%s)",
                        service.line.c_str(), state.error.c_str());
    state.error = previousError;
    if (!fetched) return false;
    JsonArrayConst stopovers = doc["trip"]["stopovers"].as<JsonArrayConst>();
    if (stopovers.isNull()) {
        log_w("[TRANSIT] Trip has no stopovers for %s", service.line.c_str());
        return false;
    }

    size_t match = stopovers.size();
    int64_t nearest = INT64_MAX;
    for (size_t i = 0; i < stopovers.size(); ++i) {
        const JsonObjectConst item = stopovers[i].as<JsonObjectConst>();
        if (service.stopId != (item["stop"]["id"] | "")) continue;
        const char *departure = item["departure"] | "";
        if (!departure[0]) departure = item["plannedDeparture"] | "";
        const int64_t epoch = util::parseIsoEpoch(departure, strlen(departure));
        if (!epoch) continue;
        const int64_t difference = epoch > service.epoch ? epoch - service.epoch : service.epoch - epoch;
        if (difference < nearest) { nearest = difference; match = i; }
    }
    if (match >= stopovers.size() || nearest > 7200) {
        log_w("[TRANSIT] Trip stop mismatch for %s (nearest=%lld, stopovers=%u)",
              service.line.c_str(), static_cast<long long>(nearest),
              static_cast<unsigned>(stopovers.size()));
        return false;
    }
    const JsonObjectConst here = stopovers[match]["stop"]["location"].as<JsonObjectConst>();
    if (here.isNull()) {
        log_w("[TRANSIT] Trip stop has no location for %s", service.line.c_str());
        return false;
    }
    const float latitude = here["latitude"] | 0.0f;
    const float longitude = here["longitude"] | 0.0f;
    for (size_t i = match + 1; i < stopovers.size(); ++i) {
        const JsonObjectConst next = stopovers[i]["stop"]["location"].as<JsonObjectConst>();
        if (next.isNull()) continue;
        const float nextLatitude = next["latitude"] | 0.0f;
        const float nextLongitude = next["longitude"] | 0.0f;
        if (normalizedVector(latitude, longitude, nextLatitude, nextLongitude, x, y) &&
            (fabsf(nextLatitude - latitude) + fabsf(nextLongitude - longitude)) >= 0.0002f)
            return true;
    }
    log_w("[TRANSIT] Trip has no next stop for %s", service.line.c_str());
    return false;
}

void resolveTravelBearings(Service services[], uint8_t count) {
    if (bearingCacheStopId != state.savedStop.id) {
        bearingCacheCount = 0;
        bearingCacheStopId = state.savedStop.id;
    }
    state.directionsPending = false;
    uint8_t probes = 0;
    for (uint8_t i = 0; i < count; ++i) {
        Service &service = services[i];
        const uint64_t key = bearingKey(service);
        BearingCacheEntry *entry = cachedBearing(key);
        const unsigned long age = entry ? millis() - entry->checkedAt : 0;
        const unsigned long retry = entry && entry->valid ? BEARING_TTL_MS : FAILED_RETRY_MS;
        if ((!entry || age >= retry) && probes < MAX_TRIP_PROBES_PER_REFRESH &&
            service.tripId.length() && service.stopId.length()) {
            float x = 0, y = 0;
            const bool valid = tripBearing(service, x, y);
            entry = &cacheSlot(key);
            entry->checkedAt = millis();
            entry->valid = valid;
            entry->x = x;
            entry->y = y;
            ++probes;
        }
        if ((!entry || (millis() - entry->checkedAt >= retry)) &&
            service.tripId.length() && service.stopId.length()) state.directionsPending = true;
        service.hasTravelBearing = entry && entry->valid;
        if (service.hasTravelBearing) {
            service.travelX = entry->x;
            service.travelY = entry->y;
        }
    }
}

void markHeadsignDisplay(Service services[], uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        Service &service = services[i];
        if (service.standardDirection.indexOf("Richtung ") < 0) continue;
        String other;
        bool sameTerminal = true;
        for (uint8_t j = 0; j < count; ++j) {
            const Service &peer = services[j];
            if (peer.lineId != service.lineId) continue;
            if (peer.direction != service.direction) { sameTerminal = false; break; }
            if (peer.standardDirection != service.standardDirection) {
                if (peer.standardDirection.indexOf("Richtung ") < 0 ||
                    (other.length() && other != peer.standardDirection)) {
                    sameTerminal = false;
                    break;
                }
                other = peer.standardDirection;
            }
        }
        service.useHeadsign = sameTerminal && other.length();
    }
}

bool lineAxis(const Service services[], uint8_t count, const String &lineId, float &x, float &y) {
    float bestDot = -0.5f;
    int first = -1, second = -1;
    for (uint8_t i = 0; i < count; ++i) {
        if (!services[i].hasTravelBearing || services[i].lineId != lineId) continue;
        for (uint8_t j = i + 1; j < count; ++j) {
            if (!services[j].hasTravelBearing || services[j].lineId != lineId) continue;
            const float dot = services[i].travelX * services[j].travelX +
                              services[i].travelY * services[j].travelY;
            if (dot < bestDot) { bestDot = dot; first = i; second = j; }
        }
    }
    if (first >= 0) {
        x = services[first].travelX - services[second].travelX;
        y = services[first].travelY - services[second].travelY;
        const float length = sqrtf(x * x + y * y);
        x /= length; y /= length;
        if (fabsf(x) >= fabsf(y) ? x < 0 : y < 0) { x = -x; y = -y; }
        return true;
    }
    return false;
}

uint8_t directionSide(const Service services[], uint8_t count, const Service &service) {
    float x = service.travelX, y = service.travelY;
    if (!service.hasTravelBearing && !terminalVector(service, x, y)) return 0;
    float axisX = 0, axisY = 0;
    if (lineAxis(services, count, service.lineId, axisX, axisY))
        return x * axisX + y * axisY < 0 ? 1 : 0;
    return (fabsf(x) >= fabsf(y) ? x < 0 : y < 0) ? 1 : 0;
}

uint8_t collapseAdjacentCalls(Service services[], uint8_t count, const String &selectedStopId) {
    uint8_t kept = 0;
    for (uint8_t i = 0; i < count; ++i) {
        Service &candidate = services[i];
        int duplicate = -1;
        for (uint8_t j = 0; j < kept; ++j) {
            const Service &previous = services[j];
            if (candidate.tripId.length() && previous.tripId == candidate.tripId &&
                previous.lineId == candidate.lineId &&
                previous.standardDirection == candidate.standardDirection) {
                duplicate = j;
                break;
            }
            if (!candidate.tripId.length() && !previous.tripId.length() &&
                previous.lineId == candidate.lineId && previous.stopId == candidate.stopId &&
                previous.standardDirection == candidate.standardDirection &&
                previous.epoch == candidate.epoch) {
                duplicate = j;
                break;
            }
        }
        if (duplicate < 0) {
            if (kept != i) services[kept] = candidate;
            ++kept;
        } else if (candidate.stopId == selectedStopId &&
                   services[duplicate].stopId != selectedStopId) {
            services[duplicate] = candidate;
        }
    }
    std::sort(services, services + kept, [](const Service &a, const Service &b) {
        return a.epoch < b.epoch;
    });
    return kept;
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
    const String params = "&linesOfStops=false&language=en&pretty=false";
    const auto retryable = []() {
        return state.error == "Transport API response exceeds available memory" ||
               state.error == "Could not allocate transport API response" ||
               state.error == "Incomplete transport API response" ||
               state.error.startsWith("Could not parse transport API response: NoMemory") ||
               state.error.startsWith("Could not parse transport API response: IncompleteInput");
    };
    if (boardRequestStopId != state.savedStop.id) {
        boardRequestStopId = state.savedStop.id;
        compactBoardActive = false;
    }
    bool noticesUnavailable = false;
    bool fetched = false;
    if (!compactBoardActive || static_cast<int32_t>(millis() - compactBoardUntil) >= 0) {
        fetched = fetchServiceList(base + "/departures?duration=90&results=32&remarks=true" + params,
                                   "departures", departureScratch, newDepartureCount);
        if (!fetched) {
            if (!retryable()) return false;
            compactBoardUntil = millis() + COMPACT_BOARD_TTL_MS;
            compactBoardActive = true;
            state.error = "";
        } else {
            compactBoardActive = false;
        }
    }
    if (!fetched) {
        if (!fetchServiceList(base + "/departures?duration=90&results=16&remarks=true" + params,
                              "departures", departureScratch, newDepartureCount)) {
            if (!retryable()) return false;
            state.error = "";
            if (!fetchServiceList(base + "/departures?duration=90&results=16&remarks=false" + params,
                                  "departures", departureScratch, newDepartureCount)) return false;
            noticesUnavailable = true;
        }
    }

    newDepartureCount = collapseAdjacentCalls(departureScratch, newDepartureCount, state.savedStop.id);
    resolveTravelBearings(departureScratch, newDepartureCount);
    markHeadsignDisplay(departureScratch, newDepartureCount);
    uint8_t updatedACount = 0, updatedBCount = 0;
    uint8_t unverified = 0;
    for (uint8_t i = 0; i < newDepartureCount; ++i) {
        Service &service = departureScratch[i];
        const uint8_t side = directionSide(departureScratch, newDepartureCount, service);
        if (!service.hasTravelBearing) ++unverified;
        if (side == 0 && updatedACount < MAX_SERVICES / 2) {
            directionAScratch[updatedACount++] = service;
        } else if (side == 1 && updatedBCount < MAX_SERVICES / 2) {
            directionBScratch[updatedBCount++] = service;
        }
    }
    state.directionACount = updatedACount;
    state.directionBCount = updatedBCount;
    for (uint8_t i = 0; i < updatedACount; ++i) state.directionA[i] = directionAScratch[i];
    for (uint8_t i = 0; i < updatedBCount; ++i) state.directionB[i] = directionBScratch[i];
    state.boardLoaded = true;
    state.unverifiedCount = unverified;
    state.error = unverified ? "Some directions unverified" :
                  noticesUnavailable ? "Service notices unavailable" : "";
    log_i("[TRANSIT] %u departures, %u+%u shown, %u direction bearings unverified",
          newDepartureCount, updatedACount, updatedBCount, unverified);
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
    state.unverifiedCount = 0;
    state.directionsPending = false;
    bearingCacheCount = 0;
    bearingCacheStopId = state.savedStop.id;
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
