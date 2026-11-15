// SPDX-License-Identifier: GPL-3.0-or-later
#include "home_assistant.h"
#include "trusted_ca.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "../util/preferences.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

namespace homeassistant {

State state;

namespace {
constexpr unsigned long REQUEST_TIMEOUT_MS = 5000;
constexpr size_t MAX_SELECTED_IDS_BYTES = 2048;
// Home Assistant's native-app OAuth flow requires these public identifiers.
constexpr char CLIENT_ID[] = "https://home-assistant.io/android";
constexpr char REDIRECT_URI[] = "homeassistant://auth-callback";

bool isHttps() { return state.baseUrl.startsWith("https://"); }

class VerifiedWiFiClientSecure : public WiFiClientSecure {
public:
    VerifiedWiFiClientSecure() { network::configureTrustedCaBundle(*this); }
};
String requestUrl(const String &path) { return state.baseUrl + path; }

unsigned long tokenExpiry(uint32_t seconds) {
    const uint32_t bounded = seconds < 86400U ? seconds : 86400U;
    const uint32_t usable = bounded > 120U ? bounded - 60U : bounded;
    return millis() + usable * 1000UL;
}

String base64Url(const uint8_t *data, size_t length) {
    unsigned char output[96];
    size_t written = 0;
    if (mbedtls_base64_encode(output, sizeof(output), &written, data, length) != 0) return "";
    String result;
    result.reserve(written);
    for (size_t i = 0; i < written; ++i) result += static_cast<char>(output[i]);
    result.replace('+', '-');
    result.replace('/', '_');
    while (result.endsWith("=")) result.remove(result.length() - 1);
    return result;
}

String makeVerifier() {
    uint8_t randomBytes[32];
    esp_fill_random(randomBytes, sizeof(randomBytes));
    return base64Url(randomBytes, sizeof(randomBytes));
}

String makeChallenge(const String &verifier) {
    uint8_t digest[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char *>(verifier.c_str()),
                       verifier.length(), digest, 0) != 0) return "";
    return base64Url(digest, sizeof(digest));
}

String formEncode(const String &value) {
    static const char hex[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

bool storeRefreshToken() {
    Preferences preferences;
    if (!preferences.begin("sys", false)) {
        state.error = "Could not open settings storage";
        return false;
    }
    const bool saved = preferences_util::putString(preferences, "haRefresh", state.refreshToken);
    preferences.end();
    if (!saved) state.error = "Could not save Home Assistant login";
    return saved;
}

template <typename Client>
bool beginHttp(HTTPClient &http, Client &client, const String &path) {
    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(client, requestUrl(path))) {
        state.error = "Could not start Home Assistant request";
        return false;
    }
    http.addHeader("Connection", "close");
    return true;
}

template <typename Client>
int postJson(Client &client, const String &path, const String &body, String &response) {
    HTTPClient http;
    if (!beginHttp(http, client, path)) return -1;
    http.addHeader("Content-Type", "application/json");
    const int status = http.POST(body);
    if (status > 0) response = http.getString();
    http.end();
    return status;
}

template <typename Client>
int postForm(Client &client, const String &path, const String &body, String &response) {
    HTTPClient http;
    if (!beginHttp(http, client, path)) return -1;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int status = http.POST(body);
    if (status > 0) response = http.getString();
    http.end();
    return status;
}

bool supportedDomain(const String &id) {
    static const char *domains[] = {
        "light.", "switch.", "input_boolean.", "fan.", "automation.",
        "humidifier.", "siren.", "remote."
    };
    for (const char *domain : domains) if (id.startsWith(domain)) return true;
    return false;
}

bool validEntityId(const String &id) {
    const int dot = id.indexOf('.');
    if (dot <= 0 || dot == static_cast<int>(id.length()) - 1 || id.length() > 96 || !supportedDomain(id)) return false;
    for (size_t i = dot + 1; i < id.length(); ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool isSelectedId(const String &id) {
    String needle = "\n" + id + "\n";
    return ("\n" + state.selectedIds + "\n").indexOf(needle) >= 0;
}

template <typename Client>
bool loginWithClient(Client &client) {
    const String verifier = makeVerifier();
    const String challenge = makeChallenge(verifier);
    if (!verifier.length() || !challenge.length()) {
        state.error = "Could not prepare secure Home Assistant login";
        return false;
    }

    JsonDocument startBody;
    startBody["client_id"] = CLIENT_ID;
    startBody["redirect_uri"] = REDIRECT_URI;
    startBody["handler"][0] = "homeassistant";
    startBody["handler"][1] = nullptr;
    startBody["code_challenge"] = challenge;
    startBody["code_challenge_method"] = "S256";
    String request;
    serializeJson(startBody, request);
    String response;
    int status = postJson(client, "/auth/login_flow", request, response);
    if (status < 200 || status >= 300) {
        state.error = status == 401 || status == 403 ? "Home Assistant login was rejected" :
            status > 0 ? "Home Assistant returned HTTP " + String(status) : "Home Assistant connection failed";
        return false;
    }
    JsonDocument flow;
    if (deserializeJson(flow, response) || !flow["flow_id"].is<const char *>()) {
        state.error = "Home Assistant returned an invalid login flow";
        return false;
    }
    const String flowId = flow["flow_id"].as<String>();

    JsonDocument credentials;
    credentials["client_id"] = CLIENT_ID;
    credentials["username"] = state.username;
    credentials["password"] = state.password;
    serializeJson(credentials, request);
    status = postJson(client, "/auth/login_flow/" + flowId, request, response);
    if (status < 200 || status >= 300) {
        state.error = status == 401 || status == 403 ? "Home Assistant username or password rejected" :
            status > 0 ? "Home Assistant returned HTTP " + String(status) : "Home Assistant connection failed";
        return false;
    }
    JsonDocument completed;
    if (deserializeJson(completed, response)) {
        state.error = "Could not read Home Assistant login response";
        return false;
    }
    if (String(completed["type"] | "") != "create_entry") {
        state.error = String(completed["errors"]["base"] | "") == "invalid_auth" ?
            "Home Assistant username or password rejected" : "Home Assistant requires an extra login step";
        return false;
    }

    String code = completed["result"] | "";
    if (!code.length()) {
        state.error = "Home Assistant did not return a login code";
        return false;
    }
    const String form = "grant_type=authorization_code&client_id=" + formEncode(CLIENT_ID) +
        "&code=" + formEncode(code) + "&code_verifier=" + formEncode(verifier);
    status = postForm(client, "/auth/token", form, response);
    if (status < 200 || status >= 300) {
        state.error = "Could not exchange Home Assistant login code";
        return false;
    }
    JsonDocument tokens;
    if (deserializeJson(tokens, response)) {
        state.error = "Could not read Home Assistant token response";
        return false;
    }
    state.accessToken = tokens["access_token"] | "";
    state.refreshToken = tokens["refresh_token"] | "";
    const uint32_t expiresSeconds = tokens["expires_in"] | 1800U;
    if (!state.accessToken.length() || !state.refreshToken.length()) {
        state.error = "Home Assistant returned incomplete login tokens";
        return false;
    }
    state.accessExpiryMs = tokenExpiry(expiresSeconds);
    if (!storeRefreshToken()) return false;
    state.error = "";
    return true;
}

template <typename Client>
bool refreshTokenWithClient(Client &client) {
    if (!state.refreshToken.length()) return loginWithClient(client);
    const String form = "grant_type=refresh_token&client_id=" + formEncode(CLIENT_ID) +
        "&refresh_token=" + formEncode(state.refreshToken);
    String response;
    const int status = postForm(client, "/auth/token", form, response);
    if (status < 200 || status >= 300) {
        state.accessToken = "";
        // Only an invalid grant proves the saved refresh token is unusable.
        if (status == 400 || status == 401) {
            state.refreshToken = "";
            storeRefreshToken();
        }
        state.error = status == 400 || status == 401 ? "Home Assistant login expired; sign in again" :
            "Could not refresh Home Assistant login";
        return false;
    }
    JsonDocument tokens;
    if (deserializeJson(tokens, response)) {
        state.error = "Could not read refreshed Home Assistant token";
        return false;
    }
    const String accessToken = tokens["access_token"] | "";
    if (!accessToken.length()) {
        state.error = "Home Assistant returned an incomplete token response";
        return false;
    }
    const String replacement = tokens["refresh_token"] | "";
    if (replacement.length() && replacement != state.refreshToken) {
        state.refreshToken = replacement;
        if (!storeRefreshToken()) return false;
    }
    state.accessToken = accessToken;
    const uint32_t expiresSeconds = tokens["expires_in"] | 1800U;
    state.accessExpiryMs = tokenExpiry(expiresSeconds);
    state.error = "";
    return true;
}

template <typename Client>
bool ensureTokenWithClient(Client &client) {
    if (!state.accessToken.length() || static_cast<long>(millis() - state.accessExpiryMs) >= 0)
        return refreshTokenWithClient(client);
    return true;
}

template <typename Client>
bool fetchStatesWithClient(Client &client, bool retriedAfter401 = false) {
    if (!ensureTokenWithClient(client)) return false;
    HTTPClient http;
    if (!beginHttp(http, client, "/api/states")) return false;
    http.addHeader("Authorization", "Bearer " + state.accessToken);
    http.addHeader("Content-Type", "application/json");
    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        http.end();
        if (status == 401) {
            state.accessToken = "";
            state.accessExpiryMs = 0;
            if (!retriedAfter401 && refreshTokenWithClient(client))
                return fetchStatesWithClient(client, true);
        }
        state.error = status == 401 ? "Home Assistant login expired; sign in again" :
            status > 0 ? "Home Assistant returned HTTP " + String(status) : "Home Assistant connection failed";
        return false;
    }

    JsonDocument filter;
    JsonArray fields = filter.to<JsonArray>();
    JsonObject entityFilter = fields.add<JsonObject>();
    entityFilter["entity_id"] = true;
    entityFilter["state"] = true;
    entityFilter["attributes"]["friendly_name"] = true;
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (error || !doc.is<JsonArrayConst>()) {
        state.error = "Could not parse Home Assistant entity list";
        return false;
    }

    uint16_t parsedCount = 0;
    bool overflow = false;
    for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
        const String id = item["entity_id"] | "";
        if (!validEntityId(id)) continue;
        Entity candidate;
        candidate.id = id;
        candidate.name = item["attributes"]["friendly_name"] | id;
        candidate.state = item["state"] | "unavailable";
        if (candidate.name.length() > 80) candidate.name.remove(80);
        if (candidate.state.length() > 32) candidate.state.remove(32);
        candidate.selected = isSelectedId(id);
        if (parsedCount < MAX_ENTITIES) {
            state.entities[parsedCount++] = candidate;
        } else {
            overflow = true;
            uint16_t largest = 0;
            for (uint16_t i = 1; i < parsedCount; ++i)
                if (state.entities[largest].name < state.entities[i].name) largest = i;
            if (candidate.name < state.entities[largest].name) state.entities[largest] = candidate;
        }
    }
    state.count = parsedCount;
    state.selectedCount = 0;
    for (uint16_t i = 0; i < parsedCount; ++i) {
        if (state.entities[i].selected) ++state.selectedCount;
    }
    std::sort(state.entities, state.entities + parsedCount,
              [](const Entity &a, const Entity &b) { return a.name < b.name; });
    state.loaded = true;
    state.error = overflow ? "More than 256 toggles; showing first 256 alphabetically" : "";
    return true;
}

template <typename Client>
bool callToggleWithClient(Client &client, const Entity &entity, bool retriedAfter401 = false) {
    if (!ensureTokenWithClient(client)) return false;
    const int dot = entity.id.indexOf('.');
    const String domain = entity.id.substring(0, dot);
    const String action = entity.state == "on" ? "turn_off" : "turn_on";
    JsonDocument bodyDoc;
    bodyDoc["entity_id"] = entity.id;
    String body;
    serializeJson(bodyDoc, body);
    HTTPClient http;
    if (!beginHttp(http, client, "/api/services/" + domain + "/" + action)) return false;
    http.addHeader("Authorization", "Bearer " + state.accessToken);
    http.addHeader("Content-Type", "application/json");
    const int status = http.POST(body);
    http.end();
    if (status < 200 || status >= 300) {
        if (status == 401) {
            state.accessToken = "";
            state.accessExpiryMs = 0;
            if (!retriedAfter401 && refreshTokenWithClient(client))
                return callToggleWithClient(client, entity, true);
        }
        state.error = status == 401 ? "Home Assistant login expired; sign in again" :
            status > 0 ? "Home Assistant returned HTTP " + String(status) : "Home Assistant connection failed";
        return false;
    }
    return true;
}

}

String normalizedBaseUrl(const String &baseUrl) {
    String normalized = baseUrl;
    normalized.trim();
    if (normalized.length() > 160) return "";
    while (normalized.endsWith("/")) normalized.remove(normalized.length() - 1);
    if (normalized.indexOf("://") >= 0 && !normalized.startsWith("http://") && !normalized.startsWith("https://"))
        return "";
    if (normalized.length() && !normalized.startsWith("http://") && !normalized.startsWith("https://")) normalized = "http://" + normalized;
    const int schemeEnd = normalized.indexOf("://");
    if (schemeEnd >= 0) {
        const int authorityStart = schemeEnd + 3;
        int authorityEnd = normalized.indexOf('/', authorityStart);
        if (authorityEnd < 0) authorityEnd = normalized.length();
        const String authority = normalized.substring(authorityStart, authorityEnd);
        bool hasPort = false;
        if (authority.startsWith("[")) {
            const int bracket = authority.indexOf(']');
            if (bracket <= 1) return "";
            hasPort = bracket >= 0 && bracket + 1 < static_cast<int>(authority.length()) && authority[bracket + 1] == ':';
        } else {
            if (!authority.length() || authority.startsWith(":")) return "";
            hasPort = authority.indexOf(':') >= 0;
        }
        if (authority.length() && !hasPort)
            normalized = normalized.substring(0, authorityEnd) + ":8123" + normalized.substring(authorityEnd);
    }
    return normalized;
}

void loadConfig(const String &baseUrl, const String &username, const String &password,
                const String &refreshToken, const String &selectedIds) {
    state.baseUrl = normalizedBaseUrl(baseUrl);
    state.username = username.substring(0, 64);
    state.password = password.substring(0, 128);
    state.refreshToken = refreshToken.substring(0, 1024);
    state.selectedIds = selectedIds;
    if (state.selectedIds.length() > MAX_SELECTED_IDS_BYTES) {
        state.selectedIds.remove(MAX_SELECTED_IDS_BYTES);
        const int lastSeparator = state.selectedIds.lastIndexOf('\n');
        if (lastSeparator >= 0) state.selectedIds.remove(lastSeparator + 1);
    }
    state.accessToken = "";
    state.accessExpiryMs = 0;
    state.error = "";
    state.loaded = false;
    state.count = 0;
    state.selectedCount = 0;
}

bool configured() { return state.baseUrl.length() && state.username.length() && state.password.length(); }

bool authenticate() {
    if (!configured()) {
        state.error = "Enter the Home Assistant server, username and password";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        state.error = "Wi-Fi is not connected";
        return false;
    }
    if (isHttps()) {
        VerifiedWiFiClientSecure client;
        client.setHandshakeTimeout(9);
        return loginWithClient(client);
    }
    WiFiClient client;
    return loginWithClient(client);
}

bool refreshEntities() {
    if (!configured()) {
        state.error = "Set up Home Assistant first";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        state.error = "Wi-Fi is not connected";
        return false;
    }
    if (isHttps()) {
        VerifiedWiFiClientSecure client;
        client.setHandshakeTimeout(9);
        return fetchStatesWithClient(client);
    }
    WiFiClient client;
    return fetchStatesWithClient(client);
}

bool toggleEntity(uint16_t index) {
    if (index >= state.count || !state.entities[index].selected || !configured()) return false;
    if (WiFi.status() != WL_CONNECTED) {
        state.error = "Wi-Fi is not connected";
        return false;
    }
    bool success = false;
    if (isHttps()) {
        VerifiedWiFiClientSecure client;
        client.setHandshakeTimeout(9);
        success = callToggleWithClient(client, state.entities[index]);
    } else {
        WiFiClient client;
        success = callToggleWithClient(client, state.entities[index]);
    }
    if (success) refreshEntities();
    return success;
}

void saveSelection() {
    state.selectedIds = "";
    state.selectedCount = 0;
    bool truncated = false;
    for (uint16_t i = 0; i < state.count; ++i) {
        if (!state.entities[i].selected) continue;
        const size_t needed = state.entities[i].id.length() + 1;
        if (state.selectedIds.length() + needed > MAX_SELECTED_IDS_BYTES) {
            state.entities[i].selected = false;
            truncated = true;
            continue;
        }
        state.selectedIds += state.entities[i].id;
        state.selectedIds += '\n';
        ++state.selectedCount;
    }
    state.error = truncated ? "Selection limited to fit device storage" : "";
}

void restoreSavedSelection() {
    state.selectedCount = 0;
    for (uint16_t i = 0; i < state.count; ++i) {
        state.entities[i].selected = isSelectedId(state.entities[i].id);
        if (state.entities[i].selected) ++state.selectedCount;
    }
}

}
