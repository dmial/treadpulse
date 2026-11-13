// The last two weeks of walking for the statistics page: each day's walking
// time split by pulse zone, and the totals over those days.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "archive.h"
#include "pulse_zones.h"

namespace stats {

constexpr int kDays = 14;

struct Day {
    int key = 0;  // local calendar day, yyyymmdd
    int dayOfMonth = 0;
    int weekday = 0;       // 0 Monday .. 6 Sunday
    bool tracked = false;  // on or after the first day of the history
    uint32_t walkSeconds = 0;
    uint32_t pulseSeconds = 0;  // of them with a pulse from the strap
    // The same seconds by zone, as the history counted them: [0] below zone 1, [1..5] zones.
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};

    bool operator==(const Day& o) const;
    bool operator!=(const Day& o) const { return !(*this == o); }
};

struct Window {
    std::vector<Day> days;  // oldest first; the last one is today
    // Over the full days: tracked and over, so not today. A morning without a
    // walk yet would pull the averages down; today joins them tomorrow.
    int fullDays = 0;
    int goalDays = 0;  // at or over the daily goal
    uint32_t averageSeconds = 0;
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};
    // The first day of the history when it falls inside the window (0: the
    // history covers the whole window).
    int firstKey = 0;

    bool operator==(const Window& o) const;
    bool operator!=(const Window& o) const { return !(*this == o); }
};

// The `count` days up to today. `entries`: kept walks (others than these days
// are ignored); `live`: the walk in progress, or nullptr; `historyStart`: the
// start of the first walk ever kept, 0 if none yet (the history starts today).
// A walk counts on the day it started, as on the day ring. `now` must be a set
// clock; days follow the process time zone and `resetHour`.
Window lastDays(const std::vector<archive::Entry>& entries, const archive::Entry* live,
                int64_t historyStart, int64_t now, int resetHour, uint32_t goalSeconds,
                int count = kDays);

// "Nov 11" for 20261111.
std::string shortDate(int key);

}  // namespace stats
