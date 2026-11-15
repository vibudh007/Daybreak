// SPDX-License-Identifier: GPL-3.0-or-later
#include "background.h"

#include "../net/home_assistant.h"

#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace daybreak::background {
namespace {

constexpr unsigned long REFRESH_MS = 60000UL;
// Network state is guarded by networkMutex; published snapshots use cacheMutex.
SemaphoreHandle_t networkMutex = nullptr;
SemaphoreHandle_t cacheMutex = nullptr;
TaskHandle_t workerHandle = nullptr;
BoardSnapshot boardCache;
HomeSnapshot homeCache;
HaTile selectedHa[homeassistant::MAX_ENTITIES];
uint16_t selectedHaCount = 0;
String haError;
bool haLoaded = false;
std::atomic<bool> transportRequested{true};
std::atomic<bool> haRequested{true};
std::atomic<bool> weatherRequested{true};
std::atomic<bool> holidayRequested{true};
std::atomic<bool> regionRequested{true};
std::atomic<bool> regionAwaitingApply{false};
std::atomic<bool> haSelectionOpen{false};
std::atomic<uint32_t> boardRevision{0};
std::atomic<uint32_t> haRevision{0};
std::atomic<uint32_t> homeRevision{0};
Place pendingRegion;
bool regionPending = false;

void publishHome() {
    if (!cacheMutex) return;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    homeCache.weather = weather;
    homeCache.holiday = holiday;
    homeRevision.fetch_add(1, std::memory_order_release);
    xSemaphoreGive(cacheMutex);
}

void publishBoard() {
    if (!cacheMutex) return;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    const auto &source = transit::state;
    constexpr uint8_t limit = transit::MAX_SERVICES / 2;
    boardCache.stop = source.savedStop;
    boardCache.directionACount = source.directionACount < limit ? source.directionACount : limit;
    boardCache.directionBCount = source.directionBCount < limit ? source.directionBCount : limit;
    for (uint8_t i = 0; i < boardCache.directionACount; ++i)
        boardCache.directionA[i] = source.directionA[i];
    for (uint8_t i = 0; i < boardCache.directionBCount; ++i)
        boardCache.directionB[i] = source.directionB[i];
    boardCache.error = source.error;
    boardCache.loaded = source.boardLoaded;
    boardRevision.fetch_add(1, std::memory_order_release);
    xSemaphoreGive(cacheMutex);
}

void publishHa() {
    if (!cacheMutex) return;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    const auto &source = homeassistant::state;
    selectedHaCount = 0;
    for (uint16_t i = 0; i < source.count; ++i) {
        const auto &entity = source.entities[i];
        if (!entity.selected) continue;
        if (selectedHaCount >= homeassistant::MAX_ENTITIES) break;
        HaTile &tile = selectedHa[selectedHaCount++];
        tile.id = entity.id;
        tile.name = entity.name;
        tile.state = entity.state;
    }
    haError = source.error;
    haLoaded = source.loaded;
    haRevision.fetch_add(1, std::memory_order_release);
    xSemaphoreGive(cacheMutex);
}

void worker(void *) {
    // Each source refreshes independently while the UI reads stable snapshots.
    unsigned long lastTransport = 0, lastHa = 0, lastWeather = 0, lastHoliday = 0, lastRegion = 0;
    for (;;) {
        if (regionAwaitingApply.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (WiFi.status() == WL_CONNECTED) {
            const unsigned long now = millis();
            {
                NetworkLock lock;
                const unsigned long transportInterval = transit::state.directionsPending ? 15000UL : REFRESH_MS;
                if (transit::state.savedStop.id.length() &&
                    (transportRequested.exchange(false) || !lastTransport || now - lastTransport >= transportInterval)) {
                    transit::refreshBoard();
                    publishBoard();
                    log_i("[CACHE] Transport refreshed (%u + %u services)",
                          transit::state.directionACount, transit::state.directionBCount);
                    lastTransport = millis();
                }
            }
            {
                NetworkLock lock;
                if (!haSelectionOpen.load(std::memory_order_acquire) && homeassistant::configured() &&
                    (haRequested.exchange(false) || !lastHa || now - lastHa >= REFRESH_MS)) {
                    homeassistant::refreshEntities();
                    publishHa();
                    log_i("[CACHE] Home Assistant refreshed (%u selected controls)",
                          homeassistant::state.selectedCount);
                    lastHa = millis();
                }
            }
            {
                NetworkLock lock;
                if (config.automaticRegion &&
                    (regionRequested.exchange(false) || !lastRegion || now - lastRegion >= 86400000UL)) {
                    Place result;
                    if (queryAutoRegion(result)) {
                        xSemaphoreTake(cacheMutex, portMAX_DELAY);
                        pendingRegion = result;
                        regionPending = true;
                        regionAwaitingApply.store(true, std::memory_order_release);
                        xSemaphoreGive(cacheMutex);
                    }
                    lastRegion = millis();
                }
            }
            if (regionAwaitingApply.load(std::memory_order_acquire)) continue;
            {
                NetworkLock lock;
                const unsigned long weatherInterval = weather.valid ? 1800000UL : 300000UL;
                if (weatherRequested.exchange(false) || !lastWeather || now - lastWeather >= weatherInterval) {
                    fetchWeather();
                    publishHome();
                    log_i("[CACHE] Weather refreshed (%s)", weather.valid ? "ready" : "unavailable");
                    lastWeather = millis();
                }
                if (holidayRequested.exchange(false) || !lastHoliday || now - lastHoliday >= 86400000UL) {
                    fetchHoliday();
                    publishHome();
                    lastHoliday = millis();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

}

NetworkLock::NetworkLock() {
    if (networkMutex) xSemaphoreTakeRecursive(networkMutex, portMAX_DELAY);
}

NetworkLock::~NetworkLock() {
    if (networkMutex) xSemaphoreGiveRecursive(networkMutex);
}

void begin() {
    if (workerHandle) return;
    networkMutex = xSemaphoreCreateRecursiveMutex();
    cacheMutex = xSemaphoreCreateMutex();
    if (!networkMutex || !cacheMutex) {
        log_e("[CACHE] Could not allocate refresh locks");
        if (networkMutex) vSemaphoreDelete(networkMutex);
        if (cacheMutex) vSemaphoreDelete(cacheMutex);
        networkMutex = cacheMutex = nullptr;
        return;
    }
    publishBoard();
    publishHa();
    publishHome();
    if (xTaskCreatePinnedToCore(worker, "daybreak-cache", 16384, nullptr, 1,
                                &workerHandle, 0) != pdPASS) {
        workerHandle = nullptr;
        log_e("[CACHE] Could not start refresh worker");
        vSemaphoreDelete(networkMutex);
        vSemaphoreDelete(cacheMutex);
        networkMutex = cacheMutex = nullptr;
    } else log_i("[CACHE] Refresh worker started");
}

bool active() { return workerHandle != nullptr; }
void requestTransport() { transportRequested.store(true, std::memory_order_release); }
void requestHa() { haRequested.store(true, std::memory_order_release); }
void requestWeather() { weatherRequested.store(true, std::memory_order_release); }
void requestHoliday() { holidayRequested.store(true, std::memory_order_release); }
void requestAutoRegion() { regionRequested.store(true, std::memory_order_release); }
bool pendingAutoRegion(Place &target) {
    if (!cacheMutex) return false;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    const bool ready = regionPending;
    if (ready) target = pendingRegion;
    xSemaphoreGive(cacheMutex);
    return ready;
}
void finishAutoRegion() {
    if (!cacheMutex) return;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    regionPending = false;
    regionAwaitingApply.store(false, std::memory_order_release);
    xSemaphoreGive(cacheMutex);
}
void pauseHaSelection(bool paused) {
    haSelectionOpen.store(paused, std::memory_order_release);
    if (!paused) requestHa();
}

void syncBoardFromState() {
    NetworkLock lock;
    publishBoard();
}

void syncHaFromState() {
    NetworkLock lock;
    publishHa();
}

void syncHomeFromState() {
    NetworkLock lock;
    publishHome();
}

void copyHome(HomeSnapshot &target) {
    if (!cacheMutex) { target.weather = weather; target.holiday = holiday; return; }
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    target = homeCache;
    xSemaphoreGive(cacheMutex);
}

void copyBoard(BoardSnapshot &target) {
    if (!cacheMutex) {
        const auto &source = transit::state;
        constexpr uint8_t limit = transit::MAX_SERVICES / 2;
        target.stop = source.savedStop;
        target.directionACount = source.directionACount < limit ? source.directionACount : limit;
        target.directionBCount = source.directionBCount < limit ? source.directionBCount : limit;
        for (uint8_t i = 0; i < target.directionACount; ++i) target.directionA[i] = source.directionA[i];
        for (uint8_t i = 0; i < target.directionBCount; ++i) target.directionB[i] = source.directionB[i];
        target.error = source.error;
        target.loaded = source.boardLoaded;
        return;
    }
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    target = boardCache;
    xSemaphoreGive(cacheMutex);
}

void copyHaPage(uint16_t offset, HaPageSnapshot &target) {
    if (!cacheMutex) {
        // The fallback path also clears empty slots when a snapshot is reused.
        const auto &source = homeassistant::state;
        target.loaded = source.loaded;
        target.error = source.error;
        target.selectedCount = source.selectedCount;
        for (HaTile &tile : target.tiles) tile = {};
        uint16_t selected = 0;
        for (uint16_t i = 0; i < source.count; ++i) {
            const auto &entity = source.entities[i];
            if (!entity.selected) continue;
            if (selected >= offset && selected - offset < 4)
                target.tiles[selected - offset] = {entity.id, entity.name, entity.state};
            ++selected;
        }
        return;
    }
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    target.selectedCount = selectedHaCount;
    target.error = haError;
    target.loaded = haLoaded;
    for (uint8_t i = 0; i < 4; ++i) {
        const uint16_t index = offset + i;
        target.tiles[i] = index < selectedHaCount ? selectedHa[index] : HaTile{};
    }
    xSemaphoreGive(cacheMutex);
}

String haEntityId(uint16_t ordinal) {
    if (!cacheMutex) {
        uint16_t selected = 0;
        for (uint16_t i = 0; i < homeassistant::state.count; ++i) {
            if (!homeassistant::state.entities[i].selected) continue;
            if (selected++ == ordinal) return homeassistant::state.entities[i].id;
        }
        return "";
    }
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    const String result = ordinal < selectedHaCount ? selectedHa[ordinal].id : "";
    xSemaphoreGive(cacheMutex);
    return result;
}

uint32_t boardVersion() { return boardRevision.load(std::memory_order_acquire); }
uint32_t haVersion() { return haRevision.load(std::memory_order_acquire); }
uint32_t homeVersion() { return homeRevision.load(std::memory_order_acquire); }

}
