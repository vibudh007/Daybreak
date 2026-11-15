// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

// Order stores three two-bit page IDs; enabled bits refer to IDs, not positions.
struct PageRotation {
    static constexpr uint8_t count = 3;
    static constexpr uint8_t defaultOrder = 0x24;
    uint8_t order = defaultOrder;
    uint8_t enabled = 0x07;

    uint8_t pageAt(uint8_t position) const { return (order >> (position * 2)) & 3; }
    bool isEnabled(uint8_t page) const { return page < count && (enabled & (1u << page)); }
    uint8_t enabledCount() const {
        uint8_t total = 0;
        for (uint8_t page = 0; page < count; ++page) total += isEnabled(page);
        return total;
    }
    void sanitize() {
        order &= 0x3f;
        uint8_t seen = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t page = pageAt(i);
            if (page >= count || (seen & (1u << page))) { order = defaultOrder; break; }
            seen |= 1u << page;
        }
        enabled &= 0x07;
        if (!enabled) enabled = 0x07;
    }
    uint8_t first() const {
        for (uint8_t i = 0; i < count; ++i) if (isEnabled(pageAt(i))) return pageAt(i);
        return 0;
    }
    uint8_t next(uint8_t current) const {
        if (!isEnabled(current)) return first();
        uint8_t position = 0;
        while (position < count && pageAt(position) != current) ++position;
        if (position == count) return first();
        for (uint8_t step = 1; step <= count; ++step) {
            const uint8_t candidate = pageAt((position + step) % count);
            if (isEnabled(candidate)) return candidate;
        }
        return first();
    }
    bool setEnabled(uint8_t page, bool value) {
        if (page >= count || isEnabled(page) == value || (!value && enabledCount() == 1)) return false;
        if (value) enabled |= 1u << page;
        else enabled &= ~(1u << page);
        return true;
    }
    bool move(uint8_t position, int direction) {
        if (position >= count || (direction != -1 && direction != 1)) return false;
        const int destination = int(position) + direction;
        if (destination < 0 || destination >= count) return false;
        const uint8_t firstPage = pageAt(position), secondPage = pageAt(destination);
        order &= ~((3u << (position * 2)) | (3u << (destination * 2)));
        order |= (firstPage << (destination * 2)) | (secondPage << (position * 2));
        return true;
    }
};
