// SPDX-License-Identifier: GPL-3.0-or-later
#include "daybreak.h"
#include "background.h"

#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft;
bool isLightTheme = false;
const char *FIRMWARE_VERSION = "3.0.14-dev";

namespace {
XPT2046_Touchscreen touchPanel(33, 36);
unsigned long lastTap = 0;
unsigned long lastReconnect = 0;
}

void setup() {
    Serial.begin(115200);
    Serial.printf("Daybreak %s\n", FIRMWARE_VERSION);
    daybreak::loadSettings();
    isLightTheme = daybreak::config.light;
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
    tft.init();
    tft.setRotation(daybreak::config.flipped ? 3 : 1);
    tft.invertDisplay(false);
    tft.fillScreen(isLightTheme ? TFT_WHITE : TFT_BLACK);
    ledcAttach(TFT_BL, 5000, 8);
    daybreak::updateBacklight();

    SPI.begin(25, 39, 32);
    touchPanel.begin();
    touchPanel.setRotation(1);

    WiFi.mode(WIFI_STA);
    if (daybreak::config.ssid.length() && daybreak::config.password.length()) {
        WiFi.begin(daybreak::config.ssid.c_str(), daybreak::config.password.c_str());
        const unsigned long started = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - started < 10000) delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) daybreak::startClock();
    else daybreak::beginWifiSetup();
    daybreak::draw();
    daybreak::background::begin();
}

void loop() {
    const unsigned long now = millis();
    if (touchPanel.touched() && now - lastTap >= 180) {
        lastTap = now;
        const TS_Point point = touchPanel.getPoint();
        if (daybreak::view == daybreak::View::Calibration) {
            daybreak::calibrationTouch(point.x, point.y);
        } else {
            const auto &c = daybreak::config;
            const int x = constrain(map(point.x, c.touchMinX, c.touchMaxX, 0, 320), 0, 319);
            const int y = constrain(map(point.y, c.touchMinY, c.touchMaxY, 0, 240), 0, 239);
            daybreak::touch(x, y);
        }
    }
    if (WiFi.status() != WL_CONNECTED && daybreak::config.ssid.length() &&
        now - lastReconnect >= 30000 && daybreak::view != daybreak::View::Wifi &&
        daybreak::view != daybreak::View::Keyboard) {
        lastReconnect = now;
        daybreak::background::NetworkLock lock;
        WiFi.reconnect();
    }
    daybreak::tick();
    delay(20);
}
