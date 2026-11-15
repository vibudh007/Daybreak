// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace util {

inline int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

// Invalid input returns zero; transport timestamps are later than the Unix epoch.
inline int64_t parseIsoEpoch(const char *text, size_t length) {
    if (!text || length < 20 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != 't') || text[13] != ':' || text[16] != ':') return 0;
    auto number = [text, length](size_t start, size_t count, int &value) {
        if (start + count > length) return false;
        value = 0;
        for (size_t i = start; i < start + count; ++i) {
            if (text[i] < '0' || text[i] > '9') return false;
            value = value * 10 + text[i] - '0';
        }
        return true;
    };
    int year, month, day, hour, minute, second;
    if (!number(0, 4, year) || !number(5, 2, month) || !number(8, 2, day) ||
        !number(11, 2, hour) || !number(14, 2, minute) || !number(17, 2, second) ||
        year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 60) return 0;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int monthDays[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day < 1 || day > monthDays[month - 1]) return 0;

    size_t zone = 19;
    if (zone < length && text[zone] == '.') {
        const size_t fractionStart = ++zone;
        while (zone < length && text[zone] >= '0' && text[zone] <= '9') ++zone;
        if (zone == fractionStart) return 0;
    }
    int offsetSeconds = 0;
    if (zone < length && (text[zone] == '+' || text[zone] == '-')) {
        int offsetHour, offsetMinute;
        if (zone + 6 != length || text[zone + 3] != ':' ||
            !number(zone + 1, 2, offsetHour) || !number(zone + 4, 2, offsetMinute) ||
            offsetHour > 23 || offsetMinute > 59) return 0;
        offsetSeconds = (offsetHour * 60 + offsetMinute) * 60;
        if (text[zone] == '-') offsetSeconds = -offsetSeconds;
    } else if (zone + 1 != length || (text[zone] != 'Z' && text[zone] != 'z')) {
        return 0;
    }
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400LL +
        hour * 3600 + minute * 60 + second - offsetSeconds;
}

}
