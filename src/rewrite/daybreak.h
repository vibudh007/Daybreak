// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../ui/page_rotation.h"

namespace daybreak {

enum class View : uint8_t {
    Home, Settings, Wifi, Keyboard, StopResults, HaChoose, HaConfig,
    LocationResults, Calibration, WeatherSettings, RegionSettings,
    DisplaySettings, OrientationSettings, DimSettings, CoordinateSettings, PageSettings
};

enum class Edit : uint8_t {
    None, WifiPassword, WifiName, Stop, City, HaUrl, HaUser, HaPassword,
    Latitude, Longitude
};

struct WeatherDay {
    float high = 0;
    float low = 0;
    int code = 0;
};

struct WeatherHour {
    String time;
    float temperature = 0;
    int rainChance = 0;
    int code = 0;
};

struct Weather {
    bool valid = false;
    float temperature = 0;
    int humidity = 0;
    float wind = 0;
    int windDirection = 0;
    float pressure = 0;
    int code = 0;
    String sunrise;
    String sunset;
    WeatherDay next[2];
    WeatherHour hours[3];
    uint8_t hourCount = 0;
};

struct Place {
    String city = "Berlin";
    String country = "Germany";
    String iso = "DE";
    String zone = "Europe/Berlin";
    float latitude = 52.52f;
    float longitude = 13.405f;
};

struct Config {
    PageRotation pages;
    String ssid;
    String password;
    Place place;
    String posixZone = "CET-1CEST,M3.5.0,M10.5.0/3";
    int manualOffsetMinutes = 60;
    bool automaticRegion = false;
    bool manualDst = false;
    bool digital = false;
    bool twelveHour = false;
    bool seconds = true;
    bool flipped = false;
    bool light = false;
    bool fahrenheit = false;
    bool mph = false;
    bool inHg = false;
    bool autoDim = false;
    int dimStart = 22;
    int dimEnd = 6;
    int dimPercent = 20;
    int brightness = 255;
    int touchMinX = 200;
    int touchMaxX = 3900;
    int touchMinY = 200;
    int touchMaxY = 3900;
};

extern Config config;
extern Weather weather;
extern View view;
extern Edit edit;
extern String input;
extern String notice;
extern String holiday;
extern bool redraw;
extern unsigned long lastTouchMs;

void loadSettings();
void saveSettings();
void startClock();
void fetchWeather();
void fetchHoliday();
bool searchPlaces(const String &name);
uint8_t placeCount();
String placeLabel(uint8_t index);
bool choosePlace(uint8_t index);
bool refreshAutoRegion();
bool queryAutoRegion(Place &result);
bool applyAutoRegion(const Place &result);
void updateBacklight();
void beginWifiSetup();
void draw();
void touch(int x, int y);
void calibrationTouch(int rawX, int rawY);
void tick();

}
