// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

// The caller owns the text, flags, title, and buttons until this keyboard closes.
enum class TextKeyboardAction : uint8_t {
    None,
    Shift,
    ToggleNumbers,
    Delete,
    Cancel,
    Submit,
    Custom
};

struct TextKeyboardButton {
    const char *label;
    TextKeyboardAction action;
    uint8_t customId = 0;
};

struct TextKeyboardConfig {
    String *text = nullptr;
    const char *title = nullptr;
    bool *shifted = nullptr;
    bool *numbers = nullptr;
    bool maskText = false;
    bool *revealText = nullptr;
    bool showRevealButton = false;
    uint16_t maxCharacters = 64;
    const TextKeyboardButton *buttons = nullptr;
    uint8_t buttonCount = 0;
};

struct TextKeyboardEvent {
    bool handled = false;
    TextKeyboardAction action = TextKeyboardAction::None;
    uint8_t customId = 0;
};

void showTextKeyboard(const TextKeyboardConfig &config);
void drawTextKeyboard();
void redrawTextKeyboardInput();
TextKeyboardEvent handleTextKeyboardTouch(int x, int y);
