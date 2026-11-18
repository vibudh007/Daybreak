// SPDX-License-Identifier: GPL-3.0-or-later
#include "components.h"

#include "theme.h"

#include <TFT_eSPI.h>

extern TFT_eSPI tft;
namespace ui {

void fillSquircle(int x, int y, int w, int h, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, squircleRadius(w, h), color);
}

void fillSquircle(const UiRect &rect, uint16_t color) {
    fillSquircle(rect.x, rect.y, rect.w, rect.h, color);
}

uint16_t surfaceColor() { return getSurfaceColor(); }
uint16_t raisedColor() { return getRaisedColor(); }
uint16_t accentColor() { return getAccentColor(); }
uint16_t mutedTextColor() { return getMutedTextColor(); }

static uint16_t toneColor(UiTone tone) {
    switch (tone) {
        case UiTone::Primary: return accentColor();
        case UiTone::Success: return getSuccessColor();
        case UiTone::Warning: return getWarningColor();
        case UiTone::Danger: return getDangerColor();
        case UiTone::Quiet: return surfaceColor();
        case UiTone::Secondary:
        default: return raisedColor();
    }
}

static uint16_t buttonText(UiTone tone) {
    if (tone == UiTone::Quiet) return mutedTextColor();
    if (tone == UiTone::Primary || tone == UiTone::Success || tone == UiTone::Warning || tone == UiTone::Danger)
        return tone == UiTone::Primary ? getOnAccentColor() : TFT_WHITE;
    return getTextColor();
}

void drawHeaderText(const String &text, int left, int right, bool centered) {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextDatum(centered ? MC_DATUM : ML_DATUM);
    tft.setTextColor(getTextColor());
    const int maxWidth = right - left;
    String fittedTitle = text;
    if (tft.textWidth(fittedTitle) > maxWidth) {
        const String ellipsis = "...";
        while (fittedTitle.length() && tft.textWidth(fittedTitle + ellipsis) > maxWidth) {
            unsigned int end = fittedTitle.length() - 1;
            while (end > 0 && (fittedTitle[end] & 0xc0) == 0x80) --end;
            fittedTitle.remove(end);
        }
        fittedTitle += ellipsis;
    }
    tft.drawString(fittedTitle, centered ? (left + right) / 2 : left, UiLayout::HEADER_TEXT_Y);
    tft.setTextFont(1);
}

void drawHeaderAction(const UiRect &rect, const char *text) {
    fillSquircle(rect, raisedColor());
    drawHeaderText(text, rect.x + 4, rect.right() - 4, true);
}

void beginScreen(const char *title, bool showBack) {
    tft.fillScreen(getBgColor());
    const int right = showBack ? UiLayout::HEADER_BACK.x - 8 : UiLayout::SCREEN_W - UiLayout::MARGIN;
    drawHeaderText(title ? title : "", UiLayout::MARGIN, right);
    tft.drawFastHLine(UiLayout::MARGIN, UiLayout::HEADER_H, 296, surfaceColor());
    if (showBack) drawBackButton();
}

void drawBackButton() {
    fillSquircle(UiLayout::HEADER_BACK, surfaceColor());
    const int x = UiLayout::HEADER_BACK.x + UiLayout::HEADER_BACK.w / 2;
    const int y = UiLayout::HEADER_BACK.y + UiLayout::HEADER_BACK.h / 2;
    drawArrowIcon(x, y, UiArrow::Left, getTextColor());
}

void drawArrowIcon(int cx, int cy, UiArrow direction, uint16_t color) {
    if (direction == UiArrow::Up) {
        tft.fillTriangle(cx, cy - 8, cx - 7, cy, cx + 7, cy, color);
        fillSquircle(cx - 2, cy - 1, 5, 9, color);
    } else if (direction == UiArrow::Down) {
        tft.fillTriangle(cx, cy + 8, cx - 7, cy, cx + 7, cy, color);
        fillSquircle(cx - 2, cy - 7, 5, 9, color);
    } else if (direction == UiArrow::Left) {
        tft.fillTriangle(cx - 8, cy, cx, cy - 7, cx, cy + 7, color);
        fillSquircle(cx - 1, cy - 2, 9, 5, color);
    } else {
        tft.fillTriangle(cx + 8, cy, cx, cy - 7, cx, cy + 7, color);
        fillSquircle(cx - 7, cy - 2, 9, 5, color);
    }
}

void drawButton(int x, int y, int w, int h, const char *label, UiTone tone) {
    const uint16_t fill = toneColor(tone);
    fillSquircle(x, y, w, h, fill);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(buttonText(tone));
    tft.drawString(label ? label : "", x + w / 2, y + h / 2, h >= 32 ? 2 : 1);
    tft.setTextColor(getTextColor());
}

void drawButton(const UiRect &rect, const char *label, UiTone tone) {
    drawButton(rect.x, rect.y, rect.w, rect.h, label, tone);
}

void drawCard(int x, int y, int w, int h, bool raised) {
    fillSquircle(x, y, w, h, raised ? raisedColor() : surfaceColor());
}

void drawCard(const UiRect &rect, bool raised) {
    drawCard(rect.x, rect.y, rect.w, rect.h, raised);
}

void drawSectionLabel(const char *label, int x, int y) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(mutedTextColor());
    tft.drawString(label ? label : "", x, y, 1);
    tft.setTextColor(getTextColor());
}

void drawToggle(int x, int y, bool enabled, const char *label, bool interactive) {
    const int w = 50, h = 28;
    if (label && label[0]) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(getTextColor());
        tft.drawString(label, x - 8, y + h / 2, 2);
    }
    tft.fillRoundRect(x, y, w, h, h / 2, !interactive ? raisedColor() : enabled ? accentColor() : surfaceColor());
    const int r = 10;
    const int cx = enabled ? x + w - 14 : x + 14;
    tft.fillCircle(cx, y + h / 2, r, interactive ? getOnAccentColor() : mutedTextColor());
}

void drawToggleRow(int x, int y, int w, int h, const char *title, const char *stateLabel, bool enabled) {
    drawCard(x, y, w, h);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(getTextColor());
    tft.drawString(title ? title : "", x + 12, y + h / 2 - 6, 2);
    tft.setTextColor(mutedTextColor());
    tft.drawString(stateLabel ? stateLabel : "", x + 12, y + h / 2 + 10, 1);
    drawToggle(x + w - 62, y + (h - 28) / 2, enabled, nullptr);
    tft.setTextColor(getTextColor());
}

void drawToggleRow(const UiRect &rect, const char *title, const char *stateLabel, bool enabled) {
    drawToggleRow(rect.x, rect.y, rect.w, rect.h, title, stateLabel, enabled);
}

void drawListRow(int x, int y, int w, int h, const String &label, bool selected) {
    fillSquircle(x, y, w, h, selected ? accentColor() : surfaceColor());
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(selected ? getOnAccentColor() : getTextColor());
    String fittedLabel = label;
    const int maxWidth = w - (selected ? 36 : 20);
    if (tft.textWidth(fittedLabel, 2) > maxWidth) {
        const String ellipsis = "...";
        const int textWidth = maxWidth - tft.textWidth(ellipsis, 2);
        while (fittedLabel.length() && tft.textWidth(fittedLabel, 2) > textWidth)
            fittedLabel.remove(fittedLabel.length() - 1);
        fittedLabel += ellipsis;
    }
    tft.drawString(fittedLabel, x + 10, y + h / 2, 2);
    if (selected) {
        const int cx = x + w - 17, cy = y + h / 2;
        const uint16_t mark = getOnAccentColor();
        tft.drawLine(cx - 5, cy, cx - 1, cy + 4, mark);
        tft.drawLine(cx - 1, cy + 4, cx + 6, cy - 5, mark);
    }
    tft.setTextColor(getTextColor());
}

void drawListRow(const UiRect &rect, const String &label, bool selected) {
    drawListRow(rect.x, rect.y, rect.w, rect.h, label, selected);
}

void drawArrowControl(const UiRect &rect, bool up, bool enabled) {
    const uint16_t fill = enabled ? surfaceColor() : getBgColor();
    const uint16_t color = enabled ? getTextColor() : mutedTextColor();
    fillSquircle(rect, fill);
    const int cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
    drawArrowIcon(cx, cy, up ? UiArrow::Up : UiArrow::Down, color);
}

void drawArrowControl(int x, int y, bool up, bool enabled) {
    drawArrowControl(UiRect{x, y, 38, 38}, up, enabled);
}

bool isBackTouch(int x, int y) {
    return UiLayout::HEADER_BACK_HIT.contains(x, y);
}

}
