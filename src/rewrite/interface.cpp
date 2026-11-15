// SPDX-License-Identifier: GPL-3.0-or-later
#include "daybreak.h"
#include "background.h"

#include "../net/home_assistant.h"
#include "../net/transit_api.h"
#include "../ui/components.h"
#include "../ui/icons.h"
#include "../ui/pages.h"
#include "../ui/text_keyboard.h"
#include "../ui/theme.h"

#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

extern TFT_eSPI tft;
extern bool isLightTheme;

AppPage currentPage = AppPage::ClockWeather;

namespace daybreak {

View view = View::Home;
Edit edit = Edit::None;
String input;
String notice;
bool redraw = true;
unsigned long lastTouchMs = 0;

namespace {

String wifiNames[20];
String pendingWifiSsid;
int wifiCount = 0;
int listOffset = 0;
int settingsOffset = 0;
constexpr int settingsCount = 8;
int chosen = -1;
int haConfigField = -1;
bool shift = false, numbers = false, revealed = false;
unsigned long lastWeatherAttempt = 0;
unsigned long lastTransportAttempt = 0;
unsigned long lastHaAttempt = 0;
unsigned long lastDrawnSecond = 0;
unsigned long lastDestinationFrame = 0;
unsigned long lastTransportMinute = 0;
unsigned long lastDimCheck = 0;
unsigned long lastAutoRegionAttempt = 0;
bool dimmed = false;
bool wasConnected = false;
bool hourlyWeather = false;
uint32_t shownBoardVersion = 0, shownHaVersion = 0, shownHomeVersion = 0;
uint16_t haGridOffset = 0;
background::BoardSnapshot visibleBoard;
bool homeTimeReady = false;
int homeDate = -1;
int calibrationStep = 0;
int calRawX = 0, calRawY = 0;

constexpr TextKeyboardButton keyboardButtons[] = {
    {"Shift", TextKeyboardAction::Shift}, {"123", TextKeyboardAction::ToggleNumbers},
    {"Delete", TextKeyboardAction::Delete}, {"Back", TextKeyboardAction::Cancel},
    {"Enter", TextKeyboardAction::Submit}
};

void label(const String &value, int x, int y, int font = 2, uint16_t color = 0) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(font);
    tft.setTextColor(color ? color : getTextColor());
    tft.drawString(value, x, y, font);
}

String clip(String value, int width, int font = 2) {
    while (value.length() && tft.textWidth(value, font) > width) value.remove(value.length() - 1);
    return value;
}

String displayText(String value) {
    value.replace("Ä", "Ae"); value.replace("Ö", "Oe"); value.replace("Ü", "Ue");
    value.replace("ä", "ae"); value.replace("ö", "oe"); value.replace("ü", "ue");
    value.replace("ß", "ss"); value.replace("–", "-"); value.replace("—", "-");
    return value;
}

void begin(const char *title) { ui::beginScreen(title); }

void row(int n, const String &title, bool selected = false) {
    ui::drawListRow(UiLayout::listRow(n), title, selected);
}

void arrows(int count) {
    ui::drawArrowControl(UiLayout::LIST_UP, true, listOffset > 0);
    ui::drawArrowControl(UiLayout::LIST_DOWN, false, listOffset + 4 < count);
}

void header() {
    drawSettingsIcon(getAccentColor());
    if (config.pages.enabledCount() > 1) {
        const AppPage nextPage = static_cast<AppPage>(config.pages.next(static_cast<uint8_t>(currentPage)));
        drawPageButton(nextPage == AppPage::Transport ? PageButtonIcon::Transit :
                       nextPage == AppPage::HomeAssistant ? PageButtonIcon::HomeAssistant : PageButtonIcon::Clock);
    }
}

void drawWeather(const Weather &weather) {
    tft.fillRect(0, 0, 150, 240, getBgColor());
    label(clip(config.place.city, 122, 2), 9, 9, 2, getAccentColor());
    if (!weather.valid) { label("Weather unavailable", 9, 45); return; }
    drawWeatherIconVector(weather.code, 8, 34);
    float temp = config.fahrenheit ? weather.temperature * 1.8f + 32.0f : weather.temperature;
    label(String(temp, 0) + (config.fahrenheit ? " F" : " C"), 48, 44, 4);
    label("RH " + String(weather.humidity) + "%", 8, 86);
    label("Wind " + String(config.mph ? weather.wind * 0.621371f : weather.wind, 0) +
          (config.mph ? " mph" : " km/h"), 8, 105);
    label("P " + String(config.inHg ? weather.pressure * 0.02953f : weather.pressure,
          config.inHg ? 2 : 0) + (config.inHg ? " inHg" : " hPa"), 8, 124);
    label(hourlyWeather ? "NEXT HOURS" : "NEXT DAYS", 8, 143, 1, getAccentColor());
    ui::drawArrowIcon(136, 147, UiArrow::Right, getAccentColor());
    if (hourlyWeather && weather.hourCount) {
        for (uint8_t i = 0; i < weather.hourCount; ++i) {
            const int y = 155 + i * 23;
            const WeatherHour &h = weather.hours[i];
            drawWeatherIconVectorSmall(h.code, 4, y);
            const float value = config.fahrenheit ? h.temperature * 1.8f + 32.0f : h.temperature;
            label(h.time.length() >= 16 ? h.time.substring(11, 16) : "--:--", 30, y + 2, 1);
            label(String(value, 0) + (config.fahrenheit ? "F" : "C") + " " + String(h.rainChance) + "%", 73, y + 2, 1);
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            const int y = 155 + i * 32;
            drawWeatherIconVectorSmall(weather.next[i].code, 5, y);
            label(i ? "+2 days" : "Tomorrow", 38, y, 2, getMutedTextColor());
            const float high = config.fahrenheit ? weather.next[i].high * 1.8f + 32 : weather.next[i].high;
            const float low = config.fahrenheit ? weather.next[i].low * 1.8f + 32 : weather.next[i].low;
            label(String(high, 0) + "/" + String(low, 0) + (config.fahrenheit ? " F" : " C"), 38, y + 16);
        }
        if (weather.sunrise.length() >= 16 && weather.sunset.length() >= 16)
            label(weather.sunrise.substring(11, 16) + " / " + weather.sunset.substring(11, 16), 7, 224, 1);
    }
    const time_t epoch = time(nullptr);
    if (epoch >= 1700000000) {
        constexpr double lunarMonth = 29.530588853;
        double age = fmod((double(epoch) - 947182440.0) / 86400.0, lunarMonth);
        if (age < 0) age += lunarMonth;
        const int phase = int(age * 8.0 / lunarMonth + 0.5) % 8;
        drawMoonPhaseIcon(UiLayout::HOME_MOON_RIGHT - UiLayout::HOME_MOON_RADIUS,
                          UiLayout::HOME_MOON_BOTTOM - UiLayout::HOME_MOON_RADIUS,
                          UiLayout::HOME_MOON_RADIUS, phase, getTextColor(), getBgColor());
    }
}

void drawClock(bool full = true) {
    static int previousHour = -1, previousMinute = -1, previousSecond = -1;
    const uint16_t bg = getBgColor();
    if (full) {
        tft.fillRect(152, 41, 162, 143, bg);
        previousHour = previousMinute = previousSecond = -1;
    }
    time_t epoch = time(nullptr);
    struct tm local;
    localtime_r(&epoch, &local);
    if (epoch < 1700000000) { if (full) label("Syncing time", 165, 84); return; }
    if (config.digital) {
        int hour = local.tm_hour;
        if (config.twelveHour) { hour %= 12; if (!hour) hour = 12; }
        char value[16];
        snprintf(value, sizeof(value), "%02d:%02d", hour, local.tm_min);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(getTextColor(), bg);
        if (full || local.tm_hour != previousHour || local.tm_min != previousMinute)
            tft.drawString(value, UiLayout::HOME_CLOCK_CENTER_X, 93, 7);
        if (config.seconds && (full || local.tm_sec != previousSecond)) {
            char seconds[3];
            snprintf(seconds, sizeof(seconds), "%02d", local.tm_sec);
            tft.drawString(seconds, UiLayout::HOME_CLOCK_CENTER_X, 142, 4);
        }
        if (config.twelveHour && (full || local.tm_hour != previousHour)) {
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(getAccentColor(), bg);
            tft.drawString(local.tm_hour < 12 ? "AM" : "PM", 280, 145, 2);
        }
    } else {
        const int cx = UiLayout::HOME_CLOCK_CENTER_X, cy = 104, radius = 63;
        const auto hand = [&](float degrees, int length, uint16_t ink) {
            const float angle = (degrees - 90) * 0.0174532925f;
            tft.drawLine(cx, cy, cx + cosf(angle) * length, cy + sinf(angle) * length, ink);
        };
        if (!full && previousSecond >= 0) {
            hand(previousSecond * 6, 56, bg);
            if (previousMinute != local.tm_min || previousHour != local.tm_hour) {
                hand((previousHour % 12) * 30 + previousMinute * 0.5f, 34, bg);
                hand(previousMinute * 6, 49, bg);
            }
        }
        if (full) tft.drawCircle(cx, cy, radius, getTextColor());
        for (int i = 0; i < 12; ++i) {
            const float a = (i * 30 - 90) * 0.0174532925f;
            tft.fillCircle(cx + cosf(a) * 55, cy + sinf(a) * 55, 2, getTextColor());
        }
        const float hourA = ((local.tm_hour % 12) * 30 + local.tm_min * 0.5f - 90) * 0.0174532925f;
        const float minuteA = (local.tm_min * 6 - 90) * 0.0174532925f;
        const float secondA = (local.tm_sec * 6 - 90) * 0.0174532925f;
        tft.drawLine(cx, cy, cx + cosf(hourA) * 34, cy + sinf(hourA) * 34, getTextColor());
        tft.drawLine(cx, cy, cx + cosf(minuteA) * 49, cy + sinf(minuteA) * 49, getTextColor());
        tft.drawLine(cx, cy, cx + cosf(secondA) * 56, cy + sinf(secondA) * 56, getSecHandColor());
        tft.fillCircle(cx, cy, 3, getAccentColor());
    }
    previousHour = local.tm_hour;
    previousMinute = local.tm_min;
    previousSecond = local.tm_sec;
}

void drawHome() {
    const time_t epoch = time(nullptr);
    homeTimeReady = epoch >= 1700000000;
    background::HomeSnapshot visibleHome;
    background::copyHome(visibleHome);
    tft.fillScreen(getBgColor());
    drawWeather(visibleHome.weather);
    drawClock();
    const int previousDate = homeDate;
    homeDate = -1;
    if (homeTimeReady) {
        struct tm local; localtime_r(&epoch, &local);
        homeDate = local.tm_year * 366 + local.tm_yday;
        if (homeDate != previousDate) background::requestHoliday();
        static const char *weekdays[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
        static const char *months[] = {"Jan", "Feb", "Maer", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
        char date[40];
        snprintf(date, sizeof(date), "%s %02d %s %d", weekdays[local.tm_wday], local.tm_mday,
                 months[local.tm_mon], local.tm_year + 1900);
        tft.setTextFont(2);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(getTextColor());
        tft.drawString(date, UiLayout::HOME_CLOCK_CENTER_X, UiLayout::HOME_DATE_Y, 2);
    }
    if (visibleHome.holiday.length()) label(clip(visibleHome.holiday, UiLayout::PAGE_STATUS_RIGHT - 152, 1), 152, 215, 1, getAccentColor());
    else if (notice.length()) label(clip(notice, UiLayout::PAGE_STATUS_RIGHT - 152, 1), 152, 215, 1, getWarningColor());
    header();
}

String eta(const transit::Service &service) {
    const time_t now = time(nullptr);
    if (!service.epoch || now < 1700000000) return "--";
    const int64_t seconds = service.epoch - static_cast<int64_t>(now);
    if (seconds <= 0) return "now";
    const int64_t minutes = (seconds + 59) / 60;
    return minutes > 999 ? ">999m" : String(static_cast<int>(minutes)) + "m";
}

bool upcoming(const transit::Service &service, time_t now) {
    // Cached departures expire even when the network is unavailable.
    return service.epoch && (now < 1700000000 || service.epoch >= static_cast<int64_t>(now) - 60);
}

void drawDestination(const UiRect &card, const transit::Service &service) {
    static TFT_eSprite strip(&tft);
    static bool ready = false;
    if (!ready) {
        strip.setColorDepth(16);
        ready = strip.createSprite(109, 10) != nullptr;
    }
    const String destination = displayText(service.direction);
    const int width = tft.textWidth(destination, 1);
    const int overflow = max(0, width - 109);
    const unsigned long motionMs = static_cast<unsigned long>(overflow) * 95UL;
    const unsigned long cycleMs = 3000UL + motionMs + 95UL;
    const unsigned long phase = millis() % cycleMs;
    const int offset = phase < 3000UL ? 0 : min(overflow, int((phase - 3000UL) / 95UL));
    if (ready) {
        strip.fillSprite(ui::surfaceColor());
        strip.setTextFont(1); strip.setTextDatum(TL_DATUM);
        strip.setTextColor(getTextColor(), ui::surfaceColor());
        strip.drawString(destination, -offset, 0, 1);
        strip.pushSprite(card.x + 34, card.y + 17);
    } else {
        tft.fillRect(card.x + 34, card.y + 17, 109, 10, ui::surfaceColor());
        label(clip(destination, 109, 1), card.x + 34, card.y + 17, 1);
    }
}

void drawTransportDestinations() {
    const time_t now = time(nullptr);
    int row = 0;
    for (int i = 0; i < visibleBoard.directionACount && row < 4; ++i)
        if (upcoming(visibleBoard.directionA[i], now))
            drawDestination(UiLayout::TRANSPORT_ROW(0, row++), visibleBoard.directionA[i]);
    row = 0;
    for (int i = 0; i < visibleBoard.directionBCount && row < 4; ++i)
        if (upcoming(visibleBoard.directionB[i], now))
            drawDestination(UiLayout::TRANSPORT_ROW(160, row++), visibleBoard.directionB[i]);
}

void transportColumn(int x, transit::Service *services, uint8_t count) {
    const time_t now = time(nullptr);
    int row = 0;
    for (int i = 0; i < count && row < 4; ++i) {
        const transit::Service &s = services[i];
        if (!upcoming(s, now)) continue;
        const UiRect card = UiLayout::TRANSPORT_ROW(x, row++);
        ui::fillSquircle(card, ui::surfaceColor());
        drawTransitProductIcon(card.x + 3, card.y + 4, uint8_t(s.product), 26);
        label(clip(displayText(s.line), 70), card.x + 34, card.y + 2, 2);
        label(eta(s), card.x + 108, card.y + 3, 2, getAccentColor());
        String status = s.cancelled ? "Cancelled" :
            s.delaySeconds >= 60 ? String((s.delaySeconds + 30) / 60) + "m late" :
            s.delaySeconds <= -60 ? String((-s.delaySeconds + 30) / 60) + "m early" : "On time";
        if (s.remark.length()) status += " / " + displayText(s.remark);
        else if (s.platform.length()) status += " / Pl. " + displayText(s.platform);
        label(clip(status, 109, 1), card.x + 34, card.y + 29, 1, s.cancelled ? getDangerColor() : getMutedTextColor());
    }
}

void drawTransport() {
    lastTransportMinute = millis() / 60000UL;
    background::copyBoard(visibleBoard);
    tft.fillScreen(getBgColor());
    if (visibleBoard.stop.id.isEmpty()) {
        ui::drawButton(UiLayout::TRANSPORT_ADD_STOP, "Add stop", UiTone::Primary);
    } else {
        drawTransitProductIcon(10, 6, uint8_t(visibleBoard.stop.product), 26);
        label(clip(visibleBoard.stop.name, 168), 44, 12, 2);
        ui::drawButton(UiLayout::TRANSPORT_CHANGE_STOP, "Change");
        tft.drawFastVLine(UiLayout::TRANSPORT_COLUMN_DIVIDER.x, UiLayout::TRANSPORT_COLUMN_DIVIDER.y,
                          UiLayout::TRANSPORT_COLUMN_DIVIDER.h, getMutedTextColor());
        transportColumn(0, visibleBoard.directionA, visibleBoard.directionACount);
        transportColumn(160, visibleBoard.directionB, visibleBoard.directionBCount);
        drawTransportDestinations();
        if (WiFi.status() != WL_CONNECTED) label("Transport offline", 12, 224, 1, getWarningColor());
        else if (visibleBoard.error.length()) label(clip(visibleBoard.error, UiLayout::PAGE_STATUS_RIGHT - 12, 1), 12, 224, 1, getWarningColor());
        else if (!visibleBoard.loaded) label("Loading departures", 12, 224, 1, getMutedTextColor());
    }
    header();
}

void drawHa() {
    background::HaPageSnapshot page;
    background::copyHaPage(haGridOffset, page);
    const uint16_t maxOffset = homeassistant::selectedGridMaxOffset(page.selectedCount);
    if (haGridOffset > maxOffset) {
        haGridOffset = maxOffset;
        background::copyHaPage(haGridOffset, page);
    }
    tft.fillScreen(getBgColor());
    if (!homeassistant::configured()) {
        ui::drawButton(UiLayout::HA_LOGIN, "Log Into Home Assistant", UiTone::Primary);
    } else {
        label("Home Assistant", 12, 12, 2);
        ui::drawButton(UiLayout::HA_CHANGE, "Change");
        if (!page.loaded) {
            label("Loading controls", 12, 74, 2, getMutedTextColor());
        } else if (page.selectedCount == 0) {
            ui::drawButton(UiLayout::HA_CHOOSE_EMPTY, "Choose controls", UiTone::Primary);
        } else {
            for (int tile = 0; tile < 4; ++tile) {
                if (haGridOffset + tile >= page.selectedCount) break;
                const auto &e = page.tiles[tile];
                const UiRect box = UiLayout::haTile(tile);
                ui::fillSquircle(box, e.state == "on" ? getRaisedColor() : getSurfaceColor());
                label(clip(e.name, box.w - 16), box.x + 8, box.y + 13, 2);
                label(e.state, box.x + 8, box.y + 43, 2, e.state == "on" ? getSuccessColor() : getMutedTextColor());
            }
            ui::drawArrowControl(UiLayout::HA_GRID_PREVIOUS, true, haGridOffset > 0);
            ui::drawArrowControl(UiLayout::HA_GRID_NEXT, false,
                haGridOffset < homeassistant::selectedGridMaxOffset(page.selectedCount));
        }
        if (page.error.length()) {
            const int x = page.selectedCount ? 100 : 12;
            label(clip(page.error, UiLayout::PAGE_STATUS_RIGHT - x, 1), x, 219, 1, getWarningColor());
        }
    }
    header();
}

void drawList(const char *title, const String *items, int count, int selected = -1) {
    begin(title);
    for (int i = 0; i < 4 && i + listOffset < count; ++i)
        row(i, items[i + listOffset], i + listOffset == selected);
    arrows(count);
}

void drawSettings() {
    static const String items[] = {"Wi-Fi", "Location / timezone", "Weather units", "Clock / theme",
                                   "Orientation", "Brightness / auto dim", "Calibrate touch", "Pages"};
    constexpr int count = sizeof(items) / sizeof(items[0]);
    static_assert(count == settingsCount, "Keep Settings drawing and touch count in sync");
    begin("Settings");
    for (int i = 0; i < UiLayout::SETTINGS_VISIBLE && i + settingsOffset < count; ++i)
        row(i, items[i + settingsOffset]);
    ui::drawArrowControl(UiLayout::LIST_UP, true, settingsOffset > 0);
    ui::drawArrowControl(UiLayout::LIST_DOWN, false, settingsOffset + UiLayout::SETTINGS_VISIBLE < count);
}

void drawPageArrow(const UiRect &rect, UiArrow direction, bool available) {
    ui::fillSquircle(rect, ui::surfaceColor());
    ui::drawArrowIcon(rect.x + rect.w / 2, rect.y + rect.h / 2,
                      direction, available ? getTextColor() : getMutedTextColor());
}

void drawPageSettings() {
    static const char *names[] = {"Clock / Weather", "Transport", "Home Assistant"};
    begin("Pages");
    for (uint8_t position = 0; position < PageRotation::count; ++position) {
        const uint8_t page = config.pages.pageAt(position);
        const bool enabled = config.pages.isEnabled(page);
        const bool canDisable = !enabled || config.pages.enabledCount() > 1;
        const UiRect card = UiLayout::PAGE_SETTINGS_ROW(position);
        ui::drawCard(card);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(getTextColor());
        tft.drawString(names[page], card.x + 10, card.y + card.h / 2, 2);
        drawPageArrow(UiLayout::PAGE_SETTINGS_UP(position), UiArrow::Up, position > 0);
        drawPageArrow(UiLayout::PAGE_SETTINGS_DOWN(position), UiArrow::Down,
                      position + 1 < PageRotation::count);
        const UiRect toggle = UiLayout::PAGE_SETTINGS_TOGGLE(position);
        ui::drawToggle(toggle.x, toggle.y, enabled, nullptr, canDisable);
    }
}

void drawWifi() {
    drawList("Wi-Fi networks", wifiNames, wifiCount);
    ui::drawButton(UiLayout::WIFI_OTHER_NETWORK, "Other network", UiTone::Primary);
}

void drawStopResults() {
    background::NetworkLock lock;
    String results[8];
    for (uint8_t i = 0; i < transit::state.matchCount; ++i) results[i] = transit::state.matches[i].name;
    drawList("Choose stop", results, transit::state.matchCount, chosen);
    if (!transit::state.matchCount) label(clip(transit::state.error.length() ? transit::state.error : "No matches", 250), 15, 66);
    ui::drawButton(UiLayout::TRANSPORT_SEARCH_AGAIN, "Search again");
    ui::drawButton(UiLayout::TRANSPORT_CONFIRM_STOP, "Confirm", UiTone::Primary);
}

void drawPlaceResults() {
    String results[8];
    for (uint8_t i = 0; i < placeCount(); ++i) results[i] = placeLabel(i);
    drawList("Choose location", results, placeCount(), chosen);
    if (!placeCount()) label("No matching places", 15, 66);
    ui::drawButton(UiLayout::TRANSPORT_SEARCH_AGAIN, "Search again");
    ui::drawButton(UiLayout::TRANSPORT_CONFIRM_STOP, "Confirm", UiTone::Primary);
}

void drawHaChoose() {
    background::NetworkLock lock;
    begin("Choose controls");
    for (int i = 0; i < 4 && i + listOffset < homeassistant::state.count; ++i) {
        const auto &e = homeassistant::state.entities[i + listOffset];
        row(i, e.name, e.selected);
    }
    arrows(homeassistant::state.count);
    ui::drawButton(UiLayout::HA_SELECT_CANCEL, "Back");
    ui::drawButton(UiLayout::HA_SELECT_CONFIRM, "Confirm", UiTone::Primary);
}

void drawHaConfig() {
    begin("Home Assistant");
    const String fields[] = {"URL: " + homeassistant::state.baseUrl,
                             "Username: " + homeassistant::state.username, "Password: ********"};
    for (int i = 0; i < 3; ++i) ui::drawListRow(UiLayout::HA_CONFIG_FIELD(i), fields[i]);
    ui::drawButton(UiLayout::HA_CONFIG_RECONNECT, "Reconnect");
    ui::drawButton(UiLayout::HA_CONFIG_CHOOSE, "Choose controls");
}

void drawWeatherSettings() {
    begin("Weather units");
    ui::drawToggleRow(UiLayout::WEATHER_TEMPERATURE, "Temperature", config.fahrenheit ? "F" : "C", config.fahrenheit);
    ui::drawToggleRow(UiLayout::WEATHER_WIND, "Wind", config.mph ? "mph" : "km/h", config.mph);
    ui::drawToggleRow(UiLayout::WEATHER_PRESSURE, "Pressure", config.inHg ? "inHg" : "hPa", config.inHg);
    ui::drawButton(UiLayout::WEATHER_COORD_EDIT, "Coords");
}

void drawRegionSettings() {
    begin("Location / timezone");
    label(clip(config.place.city + ", " + config.place.country, 296), 12, 62);
    ui::drawButton(UiLayout::REGION_CHANGE_LOCATION, "Change location", UiTone::Primary);
    ui::drawToggleRow(UiLayout::REGION_AUTOMATIC, "Automatic region", config.automaticRegion ? "On" : "Off", config.automaticRegion);
    ui::drawButton(UiLayout::REGION_OFFSET_MINUS, "-");
    ui::drawButton(UiLayout::REGION_OFFSET_PLUS, "+");
    label("UTC " + String(config.manualOffsetMinutes / 60) + ":" +
          (abs(config.manualOffsetMinutes % 60) < 10 ? "0" : "") + String(abs(config.manualOffsetMinutes % 60)), 12, 190);
    ui::drawToggleRow(UiLayout::REGION_DST, "DST", config.manualDst ? "On" : "Off", config.manualDst);
}

void drawDisplaySettings() {
    begin("Clock / display");
    ui::drawToggleRow(UiLayout::GRAPHICS_THEME, "Light appearance", config.light ? "On" : "Off", config.light);
    ui::drawToggleRow(UiLayout::GRAPHICS_CLOCK_STYLE, "Digital clock", config.digital ? "On" : "Off", config.digital);
    ui::drawToggleRow(UiLayout::GRAPHICS_TIME_FORMAT, "12-hour time", config.twelveHour ? "On" : "Off", config.twelveHour);
    ui::drawToggleRow(UiLayout::GRAPHICS_SECONDS, "Show seconds", config.seconds ? "On" : "Off", config.seconds);
}

void drawOrientationSettings() {
    begin("Orientation");
    ui::drawToggleRow(UiLayout::DISPLAY_ORIENTATION, "Flip display 180 degrees", config.flipped ? "On" : "Off", config.flipped);
}

void drawDimSettings() {
    begin("Display / dimming");
    ui::drawToggleRow(UiLayout::AUTODIM_TOGGLE, "Auto dim", config.autoDim ? "On" : "Off", config.autoDim);
    label("Brightness " + String(config.brightness * 100 / 255) + "%", 12, 151);
    ui::drawButton(UiLayout::DIM_BRIGHTNESS_MINUS, "-"); ui::drawButton(UiLayout::DIM_BRIGHTNESS_PLUS, "+");
    label("Start " + String(config.dimStart) + "  End " + String(config.dimEnd) +
          "  Level " + String(config.dimPercent) + "%", 12, 181);
    ui::drawButton(UiLayout::DIM_STEP_START, "Start +");
    ui::drawButton(UiLayout::DIM_STEP_END, "End +");
    ui::drawButton(UiLayout::DIM_STEP_LEVEL, "Level +");
}

void drawCoords() {
    begin("Coordinates");
    const String lat = "Lat: " + String(config.place.latitude, 4);
    const String lon = "Lon: " + String(config.place.longitude, 4);
    ui::drawButton(UiLayout::COORD_LAT_FIELD, lat.c_str());
    ui::drawButton(UiLayout::COORD_LON_FIELD, lon.c_str());
    label("Tap either value to edit", 12, 108);
}

void openKeyboard(Edit field, const String &initial = "") {
    edit = field; input = initial; shift = false;
    numbers = field == Edit::Latitude || field == Edit::Longitude;
    revealed = false;
    view = View::Keyboard;
    TextKeyboardConfig kb;
    kb.text = &input; kb.shifted = &shift; kb.numbers = &numbers;
    kb.revealText = &revealed;
    kb.maskText = field == Edit::WifiPassword || field == Edit::HaPassword;
    kb.showRevealButton = kb.maskText;
    kb.maxCharacters = field == Edit::Latitude || field == Edit::Longitude ? 12 :
                       field == Edit::HaUrl ? 160 : field == Edit::HaPassword ? 128 :
                       field == Edit::HaUser ? 64 : 96;
    kb.buttons = keyboardButtons; kb.buttonCount = 5;
    showTextKeyboard(kb);
    redraw = false;
}

void scanWifi() {
    background::NetworkLock lock;
    wifiCount = 0;
    const int found = WiFi.scanNetworks();
    for (int i = 0; i < found && wifiCount < 20; ++i) wifiNames[wifiCount++] = WiFi.SSID(i);
    WiFi.scanDelete(); listOffset = 0; view = View::Wifi; redraw = true;
}

bool connectWifi(const String &name, const String &secret) {
    background::NetworkLock lock;
    WiFi.mode(WIFI_STA);
    WiFi.begin(name.c_str(), secret.c_str());
    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        if (config.ssid.length() && config.password.length())
            WiFi.begin(config.ssid.c_str(), config.password.c_str());
        return false;
    }
    config.ssid = name; config.password = secret;
    saveSettings(); startClock();
    lastWeatherAttempt = lastTransportAttempt = lastHaAttempt = 0;
    background::requestTransport();
    background::requestHa();
    background::requestWeather();
    background::requestHoliday();
    background::requestAutoRegion();
    view = View::Home; redraw = true;
    return true;
}

void manualZone() {
    config.automaticRegion = false;
    const int total = config.manualOffsetMinutes + (config.manualDst ? 60 : 0);
    config.posixZone = String("UTC") + (total <= 0 ? "+" : "-") +
        String(abs(total) / 60) + ":" + (abs(total) % 60 < 10 ? "0" : "") + String(abs(total) % 60);
    startClock(); saveSettings(); redraw = true;
}

void backToSettings() { view = View::Settings; listOffset = settingsOffset; redraw = true; }

void submitKeyboard() {
    const String value = input;
    if (edit == Edit::WifiName) { pendingWifiSsid = value; openKeyboard(Edit::WifiPassword); return; }
    if (edit == Edit::WifiPassword) {
        if (!connectWifi(pendingWifiSsid, value)) { notice = "Wi-Fi connection failed"; scanWifi(); }
        return;
    }
    if (edit == Edit::Stop) {
        background::NetworkLock lock;
        transit::state.query = value;
        transit::searchStops(value);
        chosen = -1; listOffset = 0; view = View::StopResults; redraw = true;
        return;
    }
    if (edit == Edit::City) {
        searchPlaces(value);
        chosen = -1; listOffset = 0; view = View::LocationResults; redraw = true;
        return;
    }
    if (edit == Edit::Latitude || edit == Edit::Longitude) {
        char *end = nullptr; const float parsed = strtof(value.c_str(), &end);
        const float limit = edit == Edit::Latitude ? 90.0f : 180.0f;
        if (end == value.c_str() || *end != '\0' || !isfinite(parsed) || fabsf(parsed) > limit) {
            notice = "Invalid coordinate";
        } else {
            background::NetworkLock lock;
            if (edit == Edit::Latitude) config.place.latitude = parsed;
            else config.place.longitude = parsed;
            saveSettings(); weather.valid = false; lastWeatherAttempt = 0;
            background::syncHomeFromState(); background::requestWeather();
        }
        view = View::CoordinateSettings; redraw = true; return;
    }
    if (edit == Edit::HaUrl || edit == Edit::HaUser || edit == Edit::HaPassword) {
        background::NetworkLock lock;
        const auto &ha = homeassistant::state;
        homeassistant::loadConfig(
            edit == Edit::HaUrl ? value : ha.baseUrl,
            edit == Edit::HaUser ? value : ha.username,
            edit == Edit::HaPassword ? value : ha.password,
            "", ha.selectedIds);
        background::syncHaFromState();
    }
    if (edit == Edit::HaUrl || edit == Edit::HaUser || edit == Edit::HaPassword) {
        background::NetworkLock lock;
        saveSettings();
        if (homeassistant::configured()) {
            if (homeassistant::authenticate() && homeassistant::refreshEntities()) {
                listOffset = 0; background::pauseHaSelection(true); view = View::HaChoose;
            } else { notice = homeassistant::state.error; view = View::HaConfig; }
            background::syncHaFromState();
            background::requestHa();
        } else if (edit == Edit::HaUrl) { openKeyboard(Edit::HaUser); return; }
        else if (edit == Edit::HaUser) { openKeyboard(Edit::HaPassword); return; }
        else view = View::HaConfig;
        redraw = true;
    }
}

}

void updateBacklight() {
    time_t epoch = time(nullptr); struct tm local; localtime_r(&epoch, &local);
    bool inWindow = false;
    if (config.autoDim && epoch >= 1700000000 && config.dimStart != config.dimEnd) {
        inWindow = config.dimStart < config.dimEnd ?
            local.tm_hour >= config.dimStart && local.tm_hour < config.dimEnd :
            local.tm_hour >= config.dimStart || local.tm_hour < config.dimEnd;
    }
    dimmed = inWindow && millis() - lastTouchMs > 15000;
    const int target = dimmed ? config.brightness * config.dimPercent / 100 : config.brightness;
    ledcWrite(TFT_BL, constrain(target, 0, 255));
}

void beginWifiSetup() { scanWifi(); }

void draw() {
    if (view == View::Keyboard) { drawTextKeyboard(); return; }
    if (view == View::Home) {
        if (!config.pages.isEnabled(static_cast<uint8_t>(currentPage)))
            currentPage = static_cast<AppPage>(config.pages.first());
        if (currentPage == AppPage::ClockWeather) drawHome();
        else if (currentPage == AppPage::Transport) drawTransport();
        else drawHa();
    } else if (view == View::Settings) drawSettings();
    else if (view == View::PageSettings) drawPageSettings();
    else if (view == View::Wifi) drawWifi();
    else if (view == View::StopResults) drawStopResults();
    else if (view == View::LocationResults) drawPlaceResults();
    else if (view == View::HaChoose) drawHaChoose();
    else if (view == View::HaConfig) drawHaConfig();
    else if (view == View::WeatherSettings) drawWeatherSettings();
    else if (view == View::RegionSettings) drawRegionSettings();
    else if (view == View::DisplaySettings) drawDisplaySettings();
    else if (view == View::OrientationSettings) drawOrientationSettings();
    else if (view == View::DimSettings) drawDimSettings();
    else if (view == View::CoordinateSettings) drawCoords();
    else if (view == View::Calibration) {
        tft.fillScreen(getBgColor());
        label("Tap the cross", 100, 100);
        const int x = calibrationStep ? 296 : 24, y = calibrationStep ? 214 : 24;
        tft.drawFastHLine(x - 8, y, 16, getAccentColor());
        tft.drawFastVLine(x, y - 8, 16, getAccentColor());
    }
    redraw = false;
}

void tick() {
    const unsigned long now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            background::requestTransport(); background::requestHa();
            background::requestWeather(); background::requestHoliday();
            background::requestAutoRegion();
        }
        redraw = true;
    }
    Place automaticPlace;
    if (background::pendingAutoRegion(automaticPlace)) {
        applyAutoRegion(automaticPlace);
        background::finishAutoRegion();
    }
    const uint32_t boardVersion = background::boardVersion();
    if (shownBoardVersion != boardVersion) {
        shownBoardVersion = boardVersion;
        if (view == View::Home && currentPage == AppPage::Transport) redraw = true;
    }
    const uint32_t haVersion = background::haVersion();
    if (shownHaVersion != haVersion) {
        shownHaVersion = haVersion;
        if (view == View::Home && currentPage == AppPage::HomeAssistant) redraw = true;
    }
    const uint32_t homeVersion = background::homeVersion();
    if (shownHomeVersion != homeVersion) {
        shownHomeVersion = homeVersion;
        if (view == View::Home && currentPage == AppPage::ClockWeather) redraw = true;
    }
    if (now - lastDimCheck >= 1000) { updateBacklight(); lastDimCheck = now; }
    if (view != View::Home && view != View::Wifi &&
        !(view == View::Keyboard && (edit == Edit::WifiName || edit == Edit::WifiPassword)) &&
        now - lastTouchMs >= 180000UL) {
        if (view == View::HaChoose) {
            background::NetworkLock lock;
            homeassistant::restoreSavedSelection();
            background::pauseHaSelection(false);
        }
        view = View::Home;
        redraw = true;
    }
    if (view == View::Home && currentPage == AppPage::ClockWeather) {
        const time_t epoch = time(nullptr);
        const bool ready = epoch >= 1700000000;
        if (ready != homeTimeReady) redraw = true;
        else if (ready) {
            struct tm local;
            localtime_r(&epoch, &local);
            if (homeDate != local.tm_year * 366 + local.tm_yday) redraw = true;
        }
    }
    if (view == View::Home && currentPage == AppPage::Transport &&
        now / 60000UL != lastTransportMinute) redraw = true;
    if (redraw) { draw(); return; }
    if (!background::active() && config.automaticRegion && connected &&
        (!lastAutoRegionAttempt || now - lastAutoRegionAttempt >= 86400000UL)) {
        lastAutoRegionAttempt = now;
        refreshAutoRegion();
        lastWeatherAttempt = 0;
    }
    if (view != View::Home) return;
    if (currentPage == AppPage::ClockWeather) {
        if (!background::active() && connected &&
            (!lastWeatherAttempt || now - lastWeatherAttempt >= (weather.valid ? 1800000UL : 300000UL))) {
            lastWeatherAttempt = now;
            fetchWeather(); fetchHoliday();
            background::syncHomeFromState();
            redraw = true;
        }
        if (redraw) draw();
        else if (now / 1000 != lastDrawnSecond) { drawClock(false); lastDrawnSecond = now / 1000; }
    } else if (currentPage == AppPage::Transport) {
        if (!background::active() && transit::state.savedStop.id.length() &&
            (!lastTransportAttempt || now - lastTransportAttempt >= 60000UL)) {
            background::NetworkLock lock;
            lastTransportAttempt = now;
            transit::refreshBoard();
            background::syncBoardFromState();
            redraw = true;
        }
        if (redraw) draw();
        else if (now - lastDestinationFrame >= 250UL) {
            drawTransportDestinations();
            lastDestinationFrame = now;
        }
    } else {
        if (!background::active() && homeassistant::configured() &&
            (!lastHaAttempt || now - lastHaAttempt >= 60000UL)) {
            background::NetworkLock lock;
            lastHaAttempt = now;
            homeassistant::refreshEntities();
            background::syncHaFromState();
            redraw = true;
        }
        if (redraw) draw();
    }
}

void calibrationTouch(int rawX, int rawY) {
    if (view != View::Calibration) return;
    if (calibrationStep == 0) { calRawX = rawX; calRawY = rawY; calibrationStep = 1; redraw = true; return; }
    if (abs(rawX - calRawX) < 1000 || abs(rawY - calRawY) < 1000) {
        calibrationStep = 0; notice = "Calibration retry"; redraw = true; return;
    }
    config.touchMinX = calRawX - (rawX - calRawX) * 24 / 272;
    config.touchMaxX = rawX + (rawX - calRawX) * 24 / 272;
    config.touchMinY = calRawY - (rawY - calRawY) * 24 / 190;
    config.touchMaxY = rawY + (rawY - calRawY) * 26 / 190;
    Preferences p;
    if (p.begin("sys", false)) {
        const String suffix = config.flipped ? "F" : "";
        p.putInt((String("calXMin") + suffix).c_str(), config.touchMinX);
        p.putInt((String("calXMax") + suffix).c_str(), config.touchMaxX);
        p.putInt((String("calYMin") + suffix).c_str(), config.touchMinY);
        p.putInt((String("calYMax") + suffix).c_str(), config.touchMaxY);
        p.end();
    }
    calibrationStep = 0; backToSettings();
}

void touch(int x, int y) {
    lastTouchMs = millis();
    if (dimmed) { dimmed = false; updateBacklight(); return; }
    if (view == View::Keyboard) {
        const TextKeyboardEvent event = handleTextKeyboardTouch(x, y);
        if (event.action == TextKeyboardAction::Submit) submitKeyboard();
        else if (event.action == TextKeyboardAction::Cancel) {
            if (edit == Edit::Stop) { view = View::Home; currentPage = AppPage::Transport; }
            else if (edit == Edit::HaUrl || edit == Edit::HaUser || edit == Edit::HaPassword) view = View::HaConfig;
            else if (edit == Edit::WifiName || edit == Edit::WifiPassword) view = View::Wifi;
            else if (edit == Edit::Latitude || edit == Edit::Longitude) view = View::CoordinateSettings;
            else view = View::RegionSettings;
            redraw = true;
        }
        return;
    }
    if (view == View::Home) {
        if (config.pages.enabledCount() > 1 && UiLayout::PAGE_NEXT.contains(x, y)) {
            currentPage = static_cast<AppPage>(config.pages.next(static_cast<uint8_t>(currentPage)));
            redraw = true; return;
        }
        if (UiLayout::SETTINGS_BUTTON.contains(x, y)) { view = View::Settings; redraw = true; return; }
        if (currentPage == AppPage::ClockWeather) {
            if (UiLayout::HOME_FORECAST_HIT.contains(x,y)) { hourlyWeather = !hourlyWeather; redraw = true; }
            else if (UiLayout::CLOCK_TIME_HIT.contains(x,y)) { config.twelveHour = !config.twelveHour; saveSettings(); redraw = true; }
            else if (UiLayout::CLOCK_SECONDS_HIT.contains(x,y)) { config.seconds = !config.seconds; saveSettings(); redraw = true; }
        } else if (currentPage == AppPage::Transport) {
            if (visibleBoard.stop.id.isEmpty() ? UiLayout::TRANSPORT_ADD_STOP.contains(x,y) :
                UiLayout::TRANSPORT_CHANGE_STOP.contains(x,y)) openKeyboard(Edit::Stop);
        } else if (currentPage == AppPage::HomeAssistant) {
            background::HaPageSnapshot page;
            background::copyHaPage(haGridOffset, page);
            if (!homeassistant::configured() && UiLayout::HA_LOGIN.contains(x,y)) {
                openKeyboard(Edit::HaUrl, homeassistant::state.baseUrl); return;
            }
            else if (homeassistant::configured() && UiLayout::HA_CHANGE.contains(x,y)) { view = View::HaConfig; redraw = true; }
            else if (UiLayout::HA_CHOOSE_EMPTY.contains(x,y) && page.loaded && !page.selectedCount) {
                background::NetworkLock lock;
                background::pauseHaSelection(true);
                view = View::HaChoose; listOffset = 0; redraw = true;
            } else if (UiLayout::HA_GRID_PREVIOUS.contains(x,y) && haGridOffset >= 2) {
                haGridOffset -= 2; redraw = true;
            } else if (UiLayout::HA_GRID_NEXT.contains(x,y) &&
                       haGridOffset < homeassistant::selectedGridMaxOffset(page.selectedCount)) {
                haGridOffset += 2; redraw = true;
            } else {
                for (int tile = 0; tile < 4; ++tile) if (UiLayout::haTile(tile).contains(x,y)) {
                    const String id = background::haEntityId(haGridOffset + tile);
                    if (id.length()) {
                        background::NetworkLock lock;
                        for (uint16_t index = 0; index < homeassistant::state.count; ++index) {
                            if (homeassistant::state.entities[index].id != id) continue;
                            homeassistant::toggleEntity(index);
                            background::syncHaFromState();
                            redraw = true;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        return;
    }
    if (ui::isBackTouch(x,y)) {
        if (view == View::Settings || view == View::Wifi || view == View::StopResults || view == View::HaConfig) view = View::Home;
        else if (view == View::HaChoose) {
            background::NetworkLock lock;
            homeassistant::restoreSavedSelection();
            background::pauseHaSelection(false);
            view = View::HaConfig;
        }
        else backToSettings();
        redraw = true; return;
    }
    if (view == View::Settings) {
        if (UiLayout::LIST_UP.contains(x,y) && settingsOffset > 0) --settingsOffset;
        else if (UiLayout::LIST_DOWN.contains(x,y) && settingsOffset + UiLayout::SETTINGS_VISIBLE < settingsCount) ++settingsOffset;
        else if (UiLayout::SETTINGS_VIEWPORT.contains(x,y)) {
            const int visibleRow = (y - UiLayout::LIST_TOP) / UiLayout::LIST_ROW_STEP;
            if (!UiLayout::listRow(visibleRow).contains(x, y)) return;
            const int index = settingsOffset + visibleRow;
            if (index == 0) scanWifi();
            else if (index == 1) view = View::RegionSettings;
            else if (index == 2) view = View::WeatherSettings;
            else if (index == 3) view = View::DisplaySettings;
            else if (index == 4) view = View::OrientationSettings;
            else if (index == 5) view = View::DimSettings;
            else if (index == 6) { view = View::Calibration; calibrationStep = 0; }
            else if (index == 7) view = View::PageSettings;
        }
        redraw = true; return;
    }
    if (view == View::PageSettings) {
        for (uint8_t position = 0; position < PageRotation::count; ++position) {
            if (!UiLayout::PAGE_SETTINGS_SLOT(position).contains(x, y)) continue;
            bool changed = false;
            if (UiLayout::PAGE_SETTINGS_UP(position).contains(x, y))
                changed = config.pages.move(position, -1);
            else if (UiLayout::PAGE_SETTINGS_DOWN(position).contains(x, y))
                changed = config.pages.move(position, 1);
            else if (UiLayout::PAGE_SETTINGS_ROW(position).contains(x, y)) {
                const uint8_t page = config.pages.pageAt(position);
                changed = config.pages.setEnabled(page, !config.pages.isEnabled(page));
                if (changed && !config.pages.isEnabled(static_cast<uint8_t>(currentPage)))
                    currentPage = static_cast<AppPage>(config.pages.first());
            }
            if (changed) { saveSettings(); redraw = true; }
            return;
        }
        return;
    }
    if (view == View::Wifi || view == View::StopResults || view == View::LocationResults || view == View::HaChoose) {
        background::NetworkLock lock;
        const int count = view == View::Wifi ? wifiCount : view == View::StopResults ? transit::state.matchCount :
                          view == View::LocationResults ? placeCount() : homeassistant::state.count;
        if (UiLayout::LIST_UP.contains(x,y) && listOffset > 0) --listOffset;
        else if (UiLayout::LIST_DOWN.contains(x,y) && listOffset + 4 < count) ++listOffset;
        else if (UiLayout::LIST_VIEWPORT.contains(x,y)) {
            const int visibleRow = (y - UiLayout::LIST_TOP) / UiLayout::LIST_ROW_STEP;
            if (!UiLayout::listRow(visibleRow).contains(x, y)) return;
            const int index = listOffset + visibleRow;
            if (index < count) {
                if (view == View::Wifi) { pendingWifiSsid = wifiNames[index]; openKeyboard(Edit::WifiPassword); return; }
                if (view == View::HaChoose) homeassistant::state.entities[index].selected =
                    !homeassistant::state.entities[index].selected;
                else chosen = index;
            }
        } else if (view == View::Wifi && UiLayout::WIFI_OTHER_NETWORK.contains(x,y)) {
            pendingWifiSsid = ""; openKeyboard(Edit::WifiName); return;
        } else if (view == View::StopResults && UiLayout::TRANSPORT_SEARCH_AGAIN.contains(x,y)) {
            openKeyboard(Edit::Stop, transit::state.query); return;
        } else if (view == View::LocationResults && UiLayout::TRANSPORT_SEARCH_AGAIN.contains(x,y)) {
            openKeyboard(Edit::City); return;
        } else if (view == View::StopResults && UiLayout::TRANSPORT_CONFIRM_STOP.contains(x,y) && chosen >= 0) {
            transit::state.selectedMatch = chosen; transit::saveSelectedStop(); saveSettings();
            background::syncBoardFromState(); background::requestTransport();
            currentPage = AppPage::Transport; view = View::Home; lastTransportAttempt = 0;
        } else if (view == View::LocationResults && UiLayout::TRANSPORT_CONFIRM_STOP.contains(x,y) && chosen >= 0) {
            choosePlace(chosen); view = View::Home; currentPage = AppPage::ClockWeather; lastWeatherAttempt = 0;
        } else if (view == View::HaChoose && UiLayout::HA_SELECT_CONFIRM.contains(x,y)) {
            homeassistant::saveSelection(); saveSettings(); view = View::Home; currentPage = AppPage::HomeAssistant;
            haGridOffset = 0; background::syncHaFromState(); background::pauseHaSelection(false);
        } else if (view == View::HaChoose && UiLayout::HA_SELECT_CANCEL.contains(x,y)) {
            homeassistant::restoreSavedSelection(); background::pauseHaSelection(false); view = View::Home;
        }
        redraw = true; return;
    }
    if (view == View::HaConfig) {
        for (int i = 0; i < 3; ++i) if (UiLayout::HA_CONFIG_FIELD(i).contains(x,y)) {
            openKeyboard(i == 0 ? Edit::HaUrl : i == 1 ? Edit::HaUser : Edit::HaPassword,
                i == 0 ? homeassistant::state.baseUrl : i == 1 ? homeassistant::state.username : ""); return;
        }
        if (UiLayout::HA_CONFIG_RECONNECT.contains(x,y)) {
            background::NetworkLock lock;
            if (homeassistant::authenticate()) homeassistant::refreshEntities();
            notice = homeassistant::state.error;
            background::syncHaFromState(); background::requestHa();
        } else if (UiLayout::HA_CONFIG_CHOOSE.contains(x,y)) {
            background::NetworkLock lock;
            background::pauseHaSelection(true);
            view = View::HaChoose; listOffset = 0;
        }
    } else if (view == View::RegionSettings) {
        if (UiLayout::REGION_CHANGE_LOCATION.contains(x,y)) { openKeyboard(Edit::City, config.place.city); return; }
        if (UiLayout::REGION_AUTOMATIC.contains(x,y)) {
            background::NetworkLock lock;
            config.automaticRegion = !config.automaticRegion;
            if (config.automaticRegion) {
                config.posixZone = "CET-1CEST,M3.5.0,M10.5.0/3";
                startClock();
                background::requestAutoRegion();
            } else manualZone();
            saveSettings();
        }
        if (UiLayout::REGION_OFFSET_MINUS.contains(x,y)) { config.manualOffsetMinutes = max(-720, config.manualOffsetMinutes - 15); manualZone(); }
        if (UiLayout::REGION_OFFSET_PLUS.contains(x,y)) { config.manualOffsetMinutes = min(840, config.manualOffsetMinutes + 15); manualZone(); }
        if (UiLayout::REGION_DST.contains(x,y)) { config.manualDst = !config.manualDst; manualZone(); }
    } else if (view == View::WeatherSettings) {
        bool changed = false;
        if (UiLayout::WEATHER_TEMPERATURE.contains(x,y)) { config.fahrenheit = !config.fahrenheit; changed = true; }
        else if (UiLayout::WEATHER_WIND.contains(x,y)) { config.mph = !config.mph; changed = true; }
        else if (UiLayout::WEATHER_PRESSURE.contains(x,y)) { config.inHg = !config.inHg; changed = true; }
        else if (UiLayout::WEATHER_COORD_EDIT.contains(x,y)) view = View::CoordinateSettings;
        if (changed) saveSettings();
    } else if (view == View::DisplaySettings) {
        bool changed = true;
        if (UiLayout::GRAPHICS_THEME.contains(x,y)) { config.light = !config.light; isLightTheme = config.light; }
        else if (UiLayout::GRAPHICS_CLOCK_STYLE.contains(x,y)) config.digital = !config.digital;
        else if (UiLayout::GRAPHICS_TIME_FORMAT.contains(x,y)) config.twelveHour = !config.twelveHour;
        else if (UiLayout::GRAPHICS_SECONDS.contains(x,y)) config.seconds = !config.seconds;
        else changed = false;
        if (changed) saveSettings();
    } else if (view == View::OrientationSettings) {
        if (UiLayout::DISPLAY_ORIENTATION.contains(x,y)) {
            config.flipped = !config.flipped; tft.setRotation(config.flipped ? 3 : 1);
            const char *suffix = config.flipped ? "F" : "";
            Preferences p;
            if (p.begin("sys", true)) {
                config.touchMinX = p.getInt((String("calXMin") + suffix).c_str(), config.flipped ? 3900 : 200);
                config.touchMaxX = p.getInt((String("calXMax") + suffix).c_str(), config.flipped ? 200 : 3900);
                config.touchMinY = p.getInt((String("calYMin") + suffix).c_str(), config.flipped ? 3900 : 200);
                config.touchMaxY = p.getInt((String("calYMax") + suffix).c_str(), config.flipped ? 200 : 3900);
                p.end();
            }
            saveSettings();
        }
    } else if (view == View::DimSettings) {
        bool changed = true;
        if (UiLayout::AUTODIM_TOGGLE.contains(x,y)) config.autoDim = !config.autoDim;
        else if (UiLayout::DIM_BRIGHTNESS_MINUS.contains(x,y)) config.brightness = max(30, config.brightness - 15);
        else if (UiLayout::DIM_BRIGHTNESS_PLUS.contains(x,y)) config.brightness = min(255, config.brightness + 15);
        else if (UiLayout::DIM_STEP_START.contains(x,y)) config.dimStart = (config.dimStart + 1) % 24;
        else if (UiLayout::DIM_STEP_END.contains(x,y)) config.dimEnd = (config.dimEnd + 1) % 24;
        else if (UiLayout::DIM_STEP_LEVEL.contains(x,y)) config.dimPercent = (config.dimPercent + 5) % 105;
        else changed = false;
        if (changed) { saveSettings(); updateBacklight(); }
    } else if (view == View::CoordinateSettings) {
        if (UiLayout::COORD_LAT_FIELD.contains(x,y)) { openKeyboard(Edit::Latitude, String(config.place.latitude, 4)); return; }
        if (UiLayout::COORD_LON_FIELD.contains(x,y)) { openKeyboard(Edit::Longitude, String(config.place.longitude, 4)); return; }
    }
    redraw = true;
}

}
