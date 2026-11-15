// SPDX-License-Identifier: GPL-3.0-or-later
#include "daybreak.h"
#include "background.h"

#include "../net/home_assistant.h"
#include "../net/transit_api.h"
#include "../net/trusted_ca.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>

namespace daybreak {

Config config;
Weather weather;
String holiday;

namespace {

struct Candidate { Place place; };
Candidate candidates[8];
uint8_t candidatesCount = 0;
String lastHolidayDate;

String queryEncode(const String &input) {
    static const char hex[] = "0123456789ABCDEF";
    String result;
    for (size_t i = 0; i < input.length(); ++i) {
        const unsigned char ch = input[i];
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.') result += char(ch);
        else { result += '%'; result += hex[ch >> 4]; result += hex[ch & 15]; }
    }
    return result;
}

bool readJson(const String &url, JsonDocument &document) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setTimeout(6500);
    WiFiClient plain;
    WiFiClientSecure secure;
    bool opened = false;
    if (url.startsWith("https://")) {
        if (!network::configureTrustedCaBundle(secure)) return false;
        opened = http.begin(secure, url);
    } else {
        opened = http.begin(plain, url);
    }
    if (!opened) return false;
    http.useHTTP10(true);
    const int status = http.GET();
    if (status != HTTP_CODE_OK) { http.end(); return false; }
    const DeserializationError error = deserializeJson(document, http.getStream());
    http.end();
    return !error;
}

bool validTouch(int a, int b) {
    return a >= 0 && b >= 0 && a < 4096 && b < 4096 && abs(a - b) >= 1000;
}

}

void loadSettings() {
    Preferences p;
    if (!p.begin("sys", true)) return;
    config.ssid = p.getString("ssid", "");
    config.password = p.getString("dbPass", "");
    config.place.city = p.getString("city", "Berlin");
    config.place.country = p.getString("country", "Germany");
    config.place.iso = p.getString("isoCode", "DE");
    config.place.zone = p.getString("timezone", "Europe/Berlin");
    config.place.latitude = p.getFloat("lat", 52.52f);
    config.place.longitude = p.getFloat("lon", 13.405f);
    config.posixZone = p.getString("posixTZ", "CET-1CEST,M3.5.0,M10.5.0/3");
    config.automaticRegion = p.getBool("regionAuto", false);
    config.manualDst = p.getBool("manualDst", false);
    config.manualOffsetMinutes = p.getInt("gmt", 3600) / 60;
    config.digital = p.getBool("digiClock", false);
    config.twelveHour = p.getBool("12hFmt", false);
    config.seconds = p.getBool("showSecs", true);
    config.flipped = p.getBool("dispFlip", false);
    config.light = p.getBool("theme", false);
    config.fahrenheit = p.getBool("weatherUnitF", false);
    config.mph = p.getBool("weatherUnitMph", false);
    config.inHg = p.getBool("weatherUnitInHg", false);
    config.autoDim = p.getBool("autoDimEnabled", false);
    config.dimStart = p.getInt("autoDimStart", 22);
    config.dimEnd = p.getInt("autoDimEnd", 6);
    config.dimPercent = p.getInt("autoDimLevel", 20);
    config.brightness = p.getInt("bright", 255);
    config.pages.order = p.getUChar("pageOrder", PageRotation::defaultOrder);
    config.pages.enabled = p.getUChar("pageEnabled", 0x07);
    const char *suffix = config.flipped ? "F" : "";
    config.touchMinX = p.getInt((String("calXMin") + suffix).c_str(), config.flipped ? 3900 : 200);
    config.touchMaxX = p.getInt((String("calXMax") + suffix).c_str(), config.flipped ? 200 : 3900);
    config.touchMinY = p.getInt((String("calYMin") + suffix).c_str(), config.flipped ? 3900 : 200);
    config.touchMaxY = p.getInt((String("calYMax") + suffix).c_str(), config.flipped ? 200 : 3900);
    transit::loadSavedStop(p.getString("trStopId", ""), p.getString("trStopName", ""),
                            p.getString("trHost", "v6.vbb.transport.rest"), p.getUChar("trProduct", 7));
    homeassistant::loadConfig(p.getString("haUrl", ""), p.getString("haUser", ""),
                              p.getString("haPass", ""), p.getString("haRefresh", ""),
                              p.getString("haSelected", ""));
    p.end();
    config.pages.sanitize();
    config.brightness = constrain(config.brightness, 30, 255);
    config.dimStart = constrain(config.dimStart, 0, 23);
    config.dimEnd = constrain(config.dimEnd, 0, 23);
    config.dimPercent = constrain(config.dimPercent, 0, 100);
    config.manualOffsetMinutes = constrain(config.manualOffsetMinutes, -720, 840);
    if (!isfinite(config.place.latitude) || fabs(config.place.latitude) > 90) config.place.latitude = 52.52f;
    if (!isfinite(config.place.longitude) || fabs(config.place.longitude) > 180) config.place.longitude = 13.405f;
    if (!validTouch(config.touchMinX, config.touchMaxX) || !validTouch(config.touchMinY, config.touchMaxY)) {
        config.touchMinX = config.touchMinY = config.flipped ? 3900 : 200;
        config.touchMaxX = config.touchMaxY = config.flipped ? 200 : 3900;
    }
}

void saveSettings() {
    background::NetworkLock lock;
    Preferences p;
    if (!p.begin("sys", false)) return;
    p.putString("ssid", config.ssid);
    p.putString("dbPass", config.password);
    p.putString("city", config.place.city);
    p.putString("country", config.place.country);
    p.putString("isoCode", config.place.iso);
    p.putString("timezone", config.place.zone);
    p.putString("posixTZ", config.posixZone);
    p.putFloat("lat", config.place.latitude);
    p.putFloat("lon", config.place.longitude);
    p.putBool("coordsValid", true);
    p.putBool("regionAuto", config.automaticRegion);
    p.putBool("manualDst", config.manualDst);
    p.putInt("gmt", config.manualOffsetMinutes * 60);
    p.putInt("dst", config.manualDst ? 3600 : 0);
    p.putBool("digiClock", config.digital);
    p.putBool("12hFmt", config.twelveHour);
    p.putBool("showSecs", config.seconds);
    p.putBool("dispFlip", config.flipped);
    p.putBool("theme", config.light);
    p.putBool("weatherUnitF", config.fahrenheit);
    p.putBool("weatherUnitMph", config.mph);
    p.putBool("weatherUnitInHg", config.inHg);
    p.putBool("autoDimEnabled", config.autoDim);
    p.putInt("autoDimStart", config.dimStart);
    p.putInt("autoDimEnd", config.dimEnd);
    p.putInt("autoDimLevel", config.dimPercent);
    p.putInt("bright", config.brightness);
    p.putUChar("pageOrder", config.pages.order);
    p.putUChar("pageEnabled", config.pages.enabled);
    p.putString("trStopId", transit::state.savedStop.id);
    p.putString("trStopName", transit::state.savedStop.name);
    p.putString("trHost", transit::state.savedStop.host);
    p.putUChar("trProduct", transit::productToStored(transit::state.savedStop.product));
    p.putString("haUrl", homeassistant::state.baseUrl);
    p.putString("haUser", homeassistant::state.username);
    p.putString("haPass", homeassistant::state.password);
    p.putString("haRefresh", homeassistant::state.refreshToken);
    p.putString("haSelected", homeassistant::state.selectedIds);
    p.end();
}

void startClock() {
    configTzTime(config.posixZone.c_str(), "pool.ntp.org", "time.cloudflare.com");
}

uint8_t placeCount() { return candidatesCount; }

String placeLabel(uint8_t index) {
    if (index >= candidatesCount) return "";
    return candidates[index].place.city + ", " + candidates[index].place.country;
}

bool searchPlaces(const String &name) {
    background::NetworkLock lock;
    candidatesCount = 0;
    if (name.length() < 2) return false;
    JsonDocument doc;
    const String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + queryEncode(name) +
                       "&count=8&language=de&countryCode=DE&format=json";
    if (!readJson(url, doc)) return false;
    for (JsonObjectConst item : doc["results"].as<JsonArrayConst>()) {
        if (candidatesCount == 8) break;
        if (!item["latitude"].is<float>() || !item["longitude"].is<float>()) continue;
        Place &p = candidates[candidatesCount++].place;
        p.city = item["name"] | "";
        p.country = item["country"] | "";
        p.iso = item["country_code"] | "";
        p.zone = item["timezone"] | "";
        p.latitude = item["latitude"].as<float>();
        p.longitude = item["longitude"].as<float>();
    }
    return candidatesCount > 0;
}

bool choosePlace(uint8_t index) {
    background::NetworkLock lock;
    if (index >= candidatesCount) return false;
    config.place = candidates[index].place;
    if (config.automaticRegion) config.posixZone = "CET-1CEST,M3.5.0,M10.5.0/3";
    saveSettings();
    startClock();
    weather.valid = false;
    holiday = "";
    lastHolidayDate = "";
    background::syncHomeFromState();
    background::requestWeather();
    background::requestHoliday();
    return true;
}

bool queryAutoRegion(Place &result) {
    background::NetworkLock lock;
    if (!config.automaticRegion || WiFi.status() != WL_CONNECTED) return false;
    JsonDocument doc;
    if (!readJson("http://ip-api.com/json/?fields=status,city,countryCode,lat,lon,timezone", doc)) return false;
    if (String(doc["status"] | "") != "success" || String(doc["countryCode"] | "") != "DE") return false;
    const String city = doc["city"] | "";
    const float latitude = doc["lat"] | NAN;
    const float longitude = doc["lon"] | NAN;
    if (!city.length() || !isfinite(latitude) || !isfinite(longitude) ||
        fabsf(latitude) > 90 || fabsf(longitude) > 180) return false;
    result.city = city;
    result.country = "Germany";
    result.iso = "DE";
    result.zone = "Europe/Berlin";
    result.latitude = latitude;
    result.longitude = longitude;
    return true;
}

bool applyAutoRegion(const Place &result) {
    background::NetworkLock lock;
    if (!config.automaticRegion) return false;
    const bool changed = config.place.city != result.city ||
        fabsf(config.place.latitude - result.latitude) > 0.01f ||
        fabsf(config.place.longitude - result.longitude) > 0.01f;
    config.place = result;
    config.posixZone = "CET-1CEST,M3.5.0,M10.5.0/3";
    if (changed) {
        weather.valid = false;
        // A changed city can change the current local holiday without changing the date.
        holiday = "";
        lastHolidayDate = "";
        saveSettings();
        background::syncHomeFromState();
        background::requestWeather();
        background::requestHoliday();
        redraw = true;
    }
    startClock();
    return true;
}

bool refreshAutoRegion() {
    Place result;
    return queryAutoRegion(result) && applyAutoRegion(result);
}

void fetchWeather() {
    background::NetworkLock lock;
    if (WiFi.status() != WL_CONNECTED) return;
    const String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(config.place.latitude, 5) +
        "&longitude=" + String(config.place.longitude, 5) +
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
        "&hourly=temperature_2m,precipitation_probability,weather_code"
        "&forecast_days=3&forecast_hours=6&timezone=auto";
    JsonDocument doc;
    if (!readJson(url, doc)) return;
    const JsonObjectConst now = doc["current"].as<JsonObjectConst>();
    const JsonObjectConst days = doc["daily"].as<JsonObjectConst>();
    if (now.isNull() || days.isNull() || days["weather_code"].as<JsonArrayConst>().size() < 3 ||
        days["temperature_2m_max"].as<JsonArrayConst>().size() < 3 ||
        days["temperature_2m_min"].as<JsonArrayConst>().size() < 3 ||
        now["temperature_2m"].isNull()) {
        return;
    }
    Weather next;
    next.valid = true;
    next.temperature = now["temperature_2m"].as<float>();
    next.humidity = now["relative_humidity_2m"] | 0;
    next.wind = now["wind_speed_10m"] | 0.0f;
    next.windDirection = now["wind_direction_10m"] | 0;
    next.pressure = now["pressure_msl"] | 0.0f;
    next.code = now["weather_code"] | 0;
    next.sunrise = days["sunrise"][0].as<String>();
    next.sunset = days["sunset"][0].as<String>();
    for (int i = 0; i < 2; ++i) {
        next.next[i].code = days["weather_code"][i + 1] | 0;
        next.next[i].high = days["temperature_2m_max"][i + 1] | 0.0f;
        next.next[i].low = days["temperature_2m_min"][i + 1] | 0.0f;
    }
    const JsonObjectConst hours = doc["hourly"].as<JsonObjectConst>();
    const JsonArrayConst times = hours["time"].as<JsonArrayConst>();
    const JsonArrayConst temperatures = hours["temperature_2m"].as<JsonArrayConst>();
    const JsonArrayConst rainChances = hours["precipitation_probability"].as<JsonArrayConst>();
    const JsonArrayConst codes = hours["weather_code"].as<JsonArrayConst>();
    next.hourCount = min(size_t(3), min(times.size(), min(temperatures.size(), codes.size())));
    for (uint8_t i = 0; i < next.hourCount; ++i) {
        next.hours[i].time = times[i].as<String>();
        next.hours[i].temperature = temperatures[i].as<float>();
        next.hours[i].rainChance = i < rainChances.size() ? rainChances[i].as<int>() : 0;
        next.hours[i].code = codes[i].as<int>();
    }
    weather = next;
}

void fetchHoliday() {
    background::NetworkLock lock;
    time_t epoch = time(nullptr);
    if (epoch < 1700000000 || WiFi.status() != WL_CONNECTED || config.place.iso.length() != 2) return;
    struct tm local;
    localtime_r(&epoch, &local);
    char date[11];
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    if (lastHolidayDate == date) return;
    JsonDocument doc;
    const String url = "https://date.nager.at/api/v3/PublicHolidays/" + String(local.tm_year + 1900) + "/" + config.place.iso;
    if (!readJson(url, doc) || !doc.is<JsonArrayConst>()) return;
    String today;
    for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
        bool localHoliday = false;
        if (config.place.city.equalsIgnoreCase("Berlin")) {
            for (const char *county : item["counties"].as<JsonArrayConst>())
                if (county && strcmp(county, "DE-BE") == 0) localHoliday = true;
        }
        if (String(item["date"] | "") == date && ((item["global"] | false) || localHoliday)) {
            today = item["localName"] | item["name"] | "";
            break;
        }
    }
    holiday = today;
    lastHolidayDate = date;
}

}
