// The last two weeks of walking for the statistics page: each day's walking
// time split by pulse zone, and the totals over those days.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <array>
#include <cstdint>
#include <map>
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
    uint32_t steps = 0;
    uint32_t pulseSeconds = 0;  // of them with a pulse from the strap
    // The same seconds by zone, as the history counted them: [0] below zone 1, [1..5] zones.
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};
    // The day's goal: its last walk's (0 without walks, or for walks kept
    // before the history had the goal).
    uint32_t goalSeconds = 0;

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
    uint32_t averageSteps = 0;
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};
    // Days in a row on goal up to today, over the whole history, not only this
    // window (DayTotals::streak(); lastDays() leaves it 0).
    int streakDays = 0;
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
// clock; days follow the process time zone and `resetHour`. A day is on goal by
// its own goal; `goalSeconds` stands in for a day without one.
Window lastDays(const std::vector<archive::Entry>& entries, const archive::Entry* live,
                int64_t historyStart, int64_t now, int resetHour, uint32_t goalSeconds,
                int count = kDays);

// Walking time and goal of every day in the history, for the days in a row on
// goal, which may go back further than any window: a few bytes a day, so
// years fit. Day keys follow the process time zone, so set it before add().
class DayTotals {
   public:
    explicit DayTotals(int resetHour) : resetHour_(resetHour) {}

    // A kept walk, on the day it started.
    void add(const archive::Entry& e);

    // Days in a row on goal up to `now`, each by its own goal (`goalSeconds`
    // for walks kept without one). Today counts once it is on goal, with
    // `live`, the walk in progress, or nullptr; until then it never breaks the
    // run. A day without walking ends it.
    int streak(int64_t now, const archive::Entry* live, uint32_t goalSeconds) const;

   private:
    struct Total {
        uint32_t walkSeconds = 0;
        uint32_t goalSeconds = 0;  // the last walk's
        int64_t lastStart = 0;
    };
    static void addTo(Total& t, const archive::Entry& e);

    int resetHour_;
    std::map<int, Total> days_;  // by yyyymmdd
};

// "Nov 11" for 20261111.
std::string shortDate(int key);

}  // namespace stats
