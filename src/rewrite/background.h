// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "daybreak.h"
#include "../net/transit_api.h"

namespace daybreak::background {

struct BoardSnapshot {
    transit::Stop stop;
    transit::Service directionA[transit::MAX_SERVICES / 2];
    transit::Service directionB[transit::MAX_SERVICES / 2];
    uint8_t directionACount = 0;
    uint8_t directionBCount = 0;
    String error;
    bool loaded = false;
};

struct HaTile {
    String id;
    String name;
    String state;
};

struct HaPageSnapshot {
    HaTile tiles[4];
    uint16_t selectedCount = 0;
    String error;
    bool loaded = false;
};

struct HomeSnapshot {
    Weather weather;
    String holiday;
};

class NetworkLock {
public:
    NetworkLock();
    ~NetworkLock();
    NetworkLock(const NetworkLock &) = delete;
    NetworkLock &operator=(const NetworkLock &) = delete;
};

void begin();
bool active();
void requestTransport();
void requestHa();
void requestWeather();
void requestHoliday();
void requestAutoRegion();
bool pendingAutoRegion(Place &target);
void finishAutoRegion();
void pauseHaSelection(bool paused);
void syncBoardFromState();
void syncHaFromState();
void syncHomeFromState();
void copyBoard(BoardSnapshot &target);
void copyHome(HomeSnapshot &target);
void copyHaPage(uint16_t offset, HaPageSnapshot &target);
String haEntityId(uint16_t ordinal);
uint32_t boardVersion();
uint32_t haVersion();
uint32_t homeVersion();

}
