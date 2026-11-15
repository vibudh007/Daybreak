// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_keyboard.h"

#include "theme.h"
#include "components.h"

#include <TFT_eSPI.h>
#include <ctype.h>

extern TFT_eSPI tft;

namespace {
TextKeyboardConfig activeConfig;
constexpr int KEY_WIDTH = UiLayout::KEYBOARD_KEY_SIZE;
constexpr UiRect ACTION_AREA = UiLayout::KEYBOARD_FOOTER;
const char *const alphaRows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char *const numberRows[] = {"1234567890", "!@#$%^&*()", "-_+=.,?/:"};

uint16_t inputBackground() { return ui::raisedColor(); }

void drawEye() {
    const int cx = UiLayout::PASSWORD_EYE_DRAW.x + UiLayout::PASSWORD_EYE_DRAW.w / 2;
    const int cy = UiLayout::PASSWORD_EYE_DRAW.y + UiLayout::PASSWORD_EYE_DRAW.h / 2;
    const uint16_t color = getTextColor();
    tft.drawLine(cx - 9, cy, cx - 5, cy - 4, color);
    tft.drawLine(cx - 5, cy - 4, cx, cy - 6, color);
    tft.drawLine(cx, cy - 6, cx + 5, cy - 4, color);
    tft.drawLine(cx + 5, cy - 4, cx + 9, cy, color);
    tft.drawLine(cx + 9, cy, cx + 5, cy + 4, color);
    tft.drawLine(cx + 5, cy + 4, cx, cy + 6, color);
    tft.drawLine(cx, cy + 6, cx - 5, cy + 4, color);
    tft.drawLine(cx - 5, cy + 4, cx - 9, cy, color);
    tft.fillCircle(cx, cy, 2, color);
    if (!activeConfig.revealText || !*activeConfig.revealText)
        tft.drawLine(cx - 6, cy + 6, cx + 6, cy - 6, color);
}
}

void showTextKeyboard(const TextKeyboardConfig &config) {
    activeConfig = config;
    drawTextKeyboard();
}

void drawTextKeyboard() {
    tft.fillScreen(getBgColor());
    tft.setTextColor(getTextColor());
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    if (activeConfig.title && activeConfig.title[0]) {
        if (tft.textWidth(activeConfig.title) > 300) tft.setFreeFont(&FreeSans9pt7b);
        tft.drawString(activeConfig.title, UiLayout::SCREEN_W / 2, 20);
    }

    redrawTextKeyboardInput();

    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(MC_DATUM);
    const bool numeric = activeConfig.numbers && *activeConfig.numbers;
    const char *const *rows = numeric ? numberRows : alphaRows;
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; rows[r][i]; ++i) {
            const int x = UiLayout::KEYBOARD_ROW_X(r, i, numeric);
            const int y = UiLayout::KEYBOARD_ROW_Y(r);
            ui::fillSquircle(x, y, KEY_WIDTH, KEY_WIDTH, ui::surfaceColor());
            char ch = rows[r][i];
            if (!numeric && activeConfig.shifted && *activeConfig.shifted) ch = toupper(ch);
            tft.drawString(String(ch), x + KEY_WIDTH / 2, y + KEY_WIDTH / 2);
        }
    }

    ui::fillSquircle(UiLayout::KEYBOARD_SPACE, ui::surfaceColor());
    tft.drawString("Space", UiLayout::KEYBOARD_SPACE.x + UiLayout::KEYBOARD_SPACE.w / 2,
                   UiLayout::KEYBOARD_SPACE.y + UiLayout::KEYBOARD_SPACE.h / 2);

    if (activeConfig.buttons && activeConfig.buttonCount) {
        for (uint8_t i = 0; i < activeConfig.buttonCount; ++i) {
            const auto &button = activeConfig.buttons[i];
            const int left = ACTION_AREA.x + i * ACTION_AREA.w / activeConfig.buttonCount + UiLayout::KEYBOARD_FOOTER_GAP / 2;
            const int right = ACTION_AREA.x + (i + 1) * ACTION_AREA.w / activeConfig.buttonCount;
            const int cellWidth = right - left;
            const UiTone tone = button.action == TextKeyboardAction::Cancel ? UiTone::Danger :
                                button.action == TextKeyboardAction::Submit ? UiTone::Primary : UiTone::Secondary;
            ui::drawButton(left, ACTION_AREA.y, cellWidth - UiLayout::KEYBOARD_FOOTER_GAP,
                           ACTION_AREA.h, button.label, tone);
        }
    }
    tft.setTextColor(getTextColor());
}

void redrawTextKeyboardInput() {
    ui::fillSquircle(UiLayout::TEXT_INPUT, inputBackground());
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(getTextColor());

    if (activeConfig.text) {
        String shown = *activeConfig.text;
        if (activeConfig.maskText && (!activeConfig.revealText || !*activeConfig.revealText)) {
            shown = "";
            for (unsigned int i = 0; i < activeConfig.text->length(); ++i) shown += '*';
        }
        const int maxWidth = activeConfig.showRevealButton ? 254 : 280;
        while (shown.length() && tft.textWidth(shown) > maxWidth) shown.remove(0, 1);
        tft.drawString(shown, 20, 55);
    }
    if (activeConfig.showRevealButton) drawEye();
}

TextKeyboardEvent handleTextKeyboardTouch(int x, int y) {
    if (!activeConfig.text) return {};

    if (activeConfig.showRevealButton && UiLayout::PASSWORD_EYE_HIT.contains(x, y)) {
        if (activeConfig.revealText) *activeConfig.revealText = !*activeConfig.revealText;
        redrawTextKeyboardInput();
        return {true, TextKeyboardAction::None, 0};
    }

    const bool numeric = activeConfig.numbers && *activeConfig.numbers;
    const char *const *rows = numeric ? numberRows : alphaRows;
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; rows[r][i]; ++i) {
            const int left = UiLayout::KEYBOARD_ROW_X(r, i, numeric);
            const int top = UiLayout::KEYBOARD_ROW_Y(r);
            const UiRect keyHit{left, top, UiLayout::KEYBOARD_KEY_HIT_W, UiLayout::KEYBOARD_KEY_HIT_H};
            if (keyHit.contains(x, y)) {
                if (activeConfig.text->length() >= activeConfig.maxCharacters) return {true, TextKeyboardAction::None, 0};
                char ch = rows[r][i];
                if (!numeric && activeConfig.shifted && *activeConfig.shifted) ch = toupper(ch);
                *activeConfig.text += ch;
                redrawTextKeyboardInput();
                return {true, TextKeyboardAction::None, 0};
            }
        }
    }

    if (UiLayout::KEYBOARD_SPACE.contains(x, y)) {
        if (activeConfig.text->length() >= activeConfig.maxCharacters) return {true, TextKeyboardAction::None, 0};
        *activeConfig.text += ' ';
        redrawTextKeyboardInput();
        return {true, TextKeyboardAction::None, 0};
    }

    if (activeConfig.buttons && activeConfig.buttonCount && ACTION_AREA.contains(x, y)) {
        const int relativeX = x - ACTION_AREA.x;
        const uint8_t index = relativeX * activeConfig.buttonCount / ACTION_AREA.w;
        if (index >= activeConfig.buttonCount) return {};
        const auto &button = activeConfig.buttons[index];
        switch (button.action) {
            case TextKeyboardAction::Shift:
                if (activeConfig.shifted) *activeConfig.shifted = !*activeConfig.shifted;
                drawTextKeyboard();
                return {true, TextKeyboardAction::None, 0};
            case TextKeyboardAction::ToggleNumbers:
                if (activeConfig.numbers) *activeConfig.numbers = !*activeConfig.numbers;
                drawTextKeyboard();
                return {true, TextKeyboardAction::None, 0};
            case TextKeyboardAction::Delete:
                if (activeConfig.text->length()) activeConfig.text->remove(activeConfig.text->length() - 1);
                redrawTextKeyboardInput();
                return {true, TextKeyboardAction::None, 0};
            case TextKeyboardAction::Cancel:
            case TextKeyboardAction::Submit:
            case TextKeyboardAction::Custom:
                return {true, button.action, button.customId};
            case TextKeyboardAction::None:
            default:
                return {true, TextKeyboardAction::None, 0};
        }
    }
    return {};
}
