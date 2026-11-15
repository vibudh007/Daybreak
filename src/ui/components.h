// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

struct UiRect {
    int x;
    int y;
    int w;
    int h;

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
};

namespace UiLayout {
// Drawing and touch handling share these coordinates in landscape orientation.
constexpr int SCREEN_W = 320, SCREEN_H = 240;
constexpr UiRect HEADER_BACK{274, 4, 42, 34};
constexpr UiRect HEADER_BACK_HIT{272, 2, 46, 38};
constexpr UiRect PAGE_NEXT{283, 3, 34, 34};
constexpr UiRect SETTINGS_BUTTON{270, 200, 50, 40};
constexpr int PAGE_STATUS_RIGHT = SETTINGS_BUTTON.x - 8;
constexpr int HOME_CLOCK_CENTER_X = 231;
constexpr int HOME_DATE_Y = 181;
constexpr int HOME_MOON_RIGHT = 146, HOME_MOON_BOTTOM = 232, HOME_MOON_RADIUS = 10;
constexpr UiRect CLOCK_TIME_HIT{160, 20, 140, 100};
constexpr UiRect CLOCK_SECONDS_HIT{160, 120, 140, 38};
constexpr UiRect HOME_FORECAST_HIT{0, 138, 151, 102};
constexpr int HEADER_H = 42;
constexpr int MARGIN = 12;
constexpr int LIST_X = 12, LIST_W = 250, LIST_ROW_H = 30, LIST_ROW_STEP = 34;
constexpr int LIST_UP_X = 270, LIST_UP_Y = 68, LIST_DOWN_X = 270, LIST_DOWN_Y = 128;
constexpr int LIST_VISIBLE = 4;
constexpr int SETTINGS_VISIBLE = 5;
constexpr int LIST_TOP = 52;
constexpr UiRect listRow(int row) { return {LIST_X, LIST_TOP + row * LIST_ROW_STEP, LIST_W, LIST_ROW_H}; }
constexpr UiRect LIST_VIEWPORT{LIST_X, LIST_TOP, LIST_W, LIST_VISIBLE * LIST_ROW_STEP};
constexpr UiRect SETTINGS_VIEWPORT{LIST_X, LIST_TOP, LIST_W, SETTINGS_VISIBLE * LIST_ROW_STEP};
constexpr UiRect LIST_UP{LIST_UP_X, LIST_UP_Y, 38, 38};
constexpr UiRect LIST_DOWN{LIST_DOWN_X, LIST_DOWN_Y, 38, 38};
constexpr UiRect FOOTER_LEFT{12, 198, 144, 34};
constexpr UiRect FOOTER_RIGHT{164, 198, 144, 34};
constexpr int PAGE_SETTINGS_TOP = 44, PAGE_SETTINGS_STEP = 66;
constexpr UiRect PAGE_SETTINGS_SLOT(int row) { return {12, PAGE_SETTINGS_TOP + row * PAGE_SETTINGS_STEP, 296, 64}; }
constexpr UiRect PAGE_SETTINGS_ROW(int row) { return {58, PAGE_SETTINGS_TOP + row * PAGE_SETTINGS_STEP, 250, 64}; }
constexpr UiRect PAGE_SETTINGS_UP(int row) { return {12, PAGE_SETTINGS_TOP + row * PAGE_SETTINGS_STEP, 38, 32}; }
constexpr UiRect PAGE_SETTINGS_DOWN(int row) { return {12, PAGE_SETTINGS_TOP + row * PAGE_SETTINGS_STEP + 32, 38, 32}; }
constexpr UiRect PAGE_SETTINGS_TOGGLE(int row) { return {246, PAGE_SETTINGS_TOP + row * PAGE_SETTINGS_STEP + 18, 50, 28}; }

constexpr UiRect TRANSPORT_ADD_STOP{55, 92, 210, 56};
constexpr UiRect PAGE_CHANGE{216, 6, 64, 28};
constexpr UiRect TRANSPORT_CHANGE_STOP = PAGE_CHANGE;
constexpr UiRect HA_LOGIN{32, 108, 256, 52};
constexpr UiRect HA_CHANGE = PAGE_CHANGE;
constexpr UiRect HA_CHOOSE_EMPTY{48, 118, 224, 42};
constexpr UiRect HA_GRID_PREVIOUS{12, 196, 38, 38};
constexpr UiRect HA_GRID_NEXT{54, 196, 38, 38};
constexpr int HA_TILE_W = 144, HA_TILE_H = 69, HA_TILE_STEP_X = 152, HA_TILE_STEP_Y = 77;
constexpr UiRect haTile(int tile) { return {12 + (tile % 2) * HA_TILE_STEP_X, 44 + (tile / 2) * HA_TILE_STEP_Y, HA_TILE_W, HA_TILE_H}; }
constexpr UiRect HA_CONFIG_RECONNECT = FOOTER_LEFT;
constexpr UiRect HA_CONFIG_CHOOSE = FOOTER_RIGHT;
constexpr UiRect HA_SELECT_CANCEL = FOOTER_LEFT;
constexpr UiRect HA_SELECT_CONFIRM = FOOTER_RIGHT;
constexpr UiRect HA_CONFIG_FIELD(int row) { return {12, 48 + row * 46, 296, 42}; }
constexpr UiRect TRANSPORT_SEARCH_AGAIN = FOOTER_LEFT;
constexpr UiRect TRANSPORT_CONFIRM_STOP = FOOTER_RIGHT;
constexpr UiRect TRANSPORT_ROW(int column, int row) { return {column + 4, 48 + row * 42, 152, 39}; }
constexpr UiRect TRANSPORT_COLUMN_DIVIDER{159, 42, 1, 172};

constexpr UiRect WIFI_OTHER_NETWORK{12, 198, 140, 34};
constexpr UiRect WEATHER_TEMPERATURE{12, 88, 296, 32};
constexpr UiRect WEATHER_WIND{12, 124, 296, 32};
constexpr UiRect WEATHER_PRESSURE{12, 160, 296, 32};
constexpr UiRect WEATHER_COORD_EDIT{232, 198, 76, 34};
constexpr UiRect REGION_CHANGE_LOCATION{12, 90, 296, 40};
constexpr UiRect REGION_AUTOMATIC{12, 136, 296, 40};
constexpr UiRect REGION_OFFSET_MINUS{116, 182, 34, 32};
constexpr UiRect REGION_OFFSET_PLUS{154, 182, 34, 32};
constexpr UiRect REGION_DST{196, 180, 112, 36};
constexpr UiRect GRAPHICS_THEME{12, 52, 296, 40};
constexpr UiRect GRAPHICS_CLOCK_STYLE{12, 98, 296, 40};
constexpr UiRect GRAPHICS_TIME_FORMAT{12, 144, 296, 40};
constexpr UiRect GRAPHICS_SECONDS{12, 190, 296, 40};
constexpr UiRect DISPLAY_ORIENTATION{12, 58, 296, 58};
constexpr UiRect AUTODIM_TOGGLE{12, 58, 296, 54};
constexpr UiRect DIM_BRIGHTNESS_MINUS{200, 144, 48, 32};
constexpr UiRect DIM_BRIGHTNESS_PLUS{260, 144, 48, 32};
constexpr UiRect DIM_STEP_START{12, 200, 92, 32};
constexpr UiRect DIM_STEP_END{114, 200, 92, 32};
constexpr UiRect DIM_STEP_LEVEL{216, 200, 92, 32};
constexpr UiRect COORD_LAT_FIELD{12, 50, 142, 34};
constexpr UiRect COORD_LON_FIELD{166, 50, 142, 34};
constexpr UiRect PASSWORD_EYE_DRAW{282, 41, 27, 28};
constexpr UiRect PASSWORD_EYE_HIT{278, 38, 34, 34};
constexpr UiRect TEXT_INPUT{10, 40, 300, 30};
constexpr UiRect KEYBOARD_SPACE{2, 170, 316, 25};
constexpr int KEYBOARD_KEY_SIZE = 26, KEYBOARD_KEY_STEP = 29;
constexpr int KEYBOARD_KEY_HIT_W = KEYBOARD_KEY_STEP, KEYBOARD_KEY_HIT_H = KEYBOARD_KEY_SIZE + 4;
constexpr int KEYBOARD_ROW_Y(int row) { return 80 + row * 30; }
constexpr int KEYBOARD_ROW_X(int row, int column, bool numeric) {
    return (numeric ? (row == 0 ? 16 : row == 1 ? 16 : 31) :
                       (row == 0 ? 16 : row == 1 ? 31 : 60)) + column * KEYBOARD_KEY_STEP;
}
constexpr UiRect KEYBOARD_FOOTER{2, 198, 316, 35};
constexpr int KEYBOARD_FOOTER_GAP = 4;
constexpr int COORD_INPUT_MAX_CHARS = 12;
static_assert(haTile(0).x == SCREEN_W - haTile(1).right(), "HA tiles need equal outer margins");
static_assert(TRANSPORT_ROW(0, 0).x == SCREEN_W - TRANSPORT_ROW(160, 0).right(), "Transport cards need equal outer margins");
static_assert(FOOTER_LEFT.w == FOOTER_RIGHT.w && FOOTER_RIGHT.right() == SCREEN_W - FOOTER_LEFT.x,
              "Footer action pair must be balanced");
static_assert(PAGE_SETTINGS_DOWN(2).bottom() == SCREEN_H &&
              PAGE_SETTINGS_ROW(0).x - PAGE_SETTINGS_UP(0).right() == 8,
              "Page controls must fit the display with an 8 px rail gap");
static_assert(REGION_OFFSET_MINUS.y >= REGION_AUTOMATIC.bottom() && REGION_DST.y >= REGION_AUTOMATIC.bottom(),
              "Region controls must clear the automatic switch");
static_assert(LIST_UP.x - listRow(0).right() == 8 && LIST_UP.right() == SCREEN_W - MARGIN,
              "List rail needs an 8 px gap and balanced outer margins");
static_assert(SCREEN_W - KEYBOARD_ROW_X(0, 9, false) - KEYBOARD_KEY_SIZE - KEYBOARD_ROW_X(0, 0, false) == 1,
              "Top letter row must be centered");
static_assert(KEYBOARD_ROW_X(1, 0, false) == SCREEN_W - KEYBOARD_ROW_X(1, 8, false) - KEYBOARD_KEY_SIZE,
              "Middle letter row must be centered");
static_assert(KEYBOARD_ROW_X(2, 0, false) == SCREEN_W - KEYBOARD_ROW_X(2, 6, false) - KEYBOARD_KEY_SIZE,
              "Bottom letter row must be centered");
}

enum class UiTone : uint8_t { Primary, Secondary, Success, Warning, Danger, Quiet };
enum class UiArrow : uint8_t { Up, Down, Left, Right };

namespace ui {
constexpr int squircleRadius(int w, int h) {
    const int shortSide = w < h ? w : h;
    const int scaled = (shortSide + 1) / 3;
    return scaled < 1 ? 1 : scaled > 16 ? 16 : scaled;
}
void fillSquircle(int x, int y, int w, int h, uint16_t color);
void fillSquircle(const UiRect &rect, uint16_t color);
uint16_t surfaceColor();
uint16_t raisedColor();
uint16_t accentColor();
uint16_t mutedTextColor();
void beginScreen(const char *title, bool showBack = true);
void drawBackButton();
void drawButton(const UiRect &rect, const char *label, UiTone tone = UiTone::Secondary);
void drawButton(int x, int y, int w, int h, const char *label, UiTone tone = UiTone::Secondary);
void drawCard(const UiRect &rect, bool raised = false);
void drawCard(int x, int y, int w, int h, bool raised = false);
void drawSectionLabel(const char *label, int x, int y);
void drawToggle(int x, int y, bool enabled, const char *label, bool interactive = true);
void drawToggleRow(const UiRect &rect, const char *title, const char *stateLabel, bool enabled);
void drawToggleRow(int x, int y, int w, int h, const char *title, const char *stateLabel, bool enabled);
void drawListRow(const UiRect &rect, const String &label, bool selected = false);
void drawListRow(int x, int y, int w, int h, const String &label, bool selected = false);
void drawArrowControl(const UiRect &rect, bool up, bool enabled = true);
void drawArrowControl(int x, int y, bool up, bool enabled = true);
void drawArrowIcon(int cx, int cy, UiArrow direction, uint16_t color);
bool isBackTouch(int x, int y);
}
