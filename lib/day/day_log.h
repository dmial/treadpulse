// Today's walking: the total toward the daily goal and the walks to mark on the
// day strip. Finished walks are deleted from flash once they are uploaded, so
// the day keeps its own short list, saved as a tiny CSV (toCsv / loadCsv).
//
// Day boundaries use the process time zone (TZ, set from user_config.h).
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace day {

struct Walk {
    int64_t startEpoch = 0;  // UTC seconds; 0 when the clock was not set
    int64_t endEpoch = 0;
    uint32_t walkSeconds = 0;  // belt moving
    // A walk of this boot can be dated later from its uptime, once the clock
    // gets set. Walks loaded from flash always have a date. Not saved.
    bool thisBoot = false;
    uint32_t startUptimeMs = 0;
    uint32_t endUptimeMs = 0;
};

// The local calendar day of a moment as yyyymmdd, for a day that starts at
// `resetHour` local time.
int dayKey(int64_t epoch, int resetHour);

// Local wall-clock seconds since midnight.
int localSecondsOfDay(int64_t epoch);

// A walk on the day strip, in local seconds since midnight.
struct StripSegment {
    int startSec = 0;
    int endSec = 0;
};

struct Today {
    uint32_t totalSeconds = 0;  // walking time toward the goal
    int walkCount = 0;
    std::vector<StripSegment> segments;  // walks with a known time
    int nowSec = -1;                     // -1 when the clock is not set
};

class DayLog {
   public:
    explicit DayLog(int resetHour = 0) : resetHour_(resetHour) {}

    void add(const Walk& w) { walks_.push_back(w); }

    // Dates the walks of this boot recorded before the clock was set. Returns
    // true when something changed (worth saving).
    bool resolve(int64_t nowEpoch, uint32_t nowUptimeMs);

    // Today as of `nowEpoch` (0 = clock not set), plus the walk in progress if
    // any. Without a clock only the undated walks of this boot count: they are
    // assumed to be today's.
    Today today(int64_t nowEpoch, const Walk* live = nullptr) const;

    // Forgets dated walks that ended more than two days before `nowEpoch`.
    void prune(int64_t nowEpoch);

    // "start_utc,end_utc,walk_s" lines of the dated walks, plus the walk in
    // progress if given (so a power cut loses at most the time since the save).
    std::string toCsv(const Walk* live = nullptr) const;
    // Replaces the list with the walks of a saved CSV; bad lines are skipped.
    void loadCsv(const std::string& csv);

    const std::vector<Walk>& walks() const { return walks_; }

   private:
    bool counts(const Walk& w, bool clockSet, int todayKey) const;

    int resetHour_;
    std::vector<Walk> walks_;
};

}  // namespace day
