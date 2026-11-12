#include "day_log.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace day {

namespace {
constexpr int kSecondsPerDay = 24 * 60 * 60;
constexpr int64_t kKeepSeconds = 2LL * kSecondsPerDay;

struct tm localTm(int64_t epoch) {
    const time_t t = static_cast<time_t>(epoch);
    struct tm local = {};
    localtime_r(&t, &local);
    return local;
}

int64_t epochAgo(int64_t nowEpoch, uint32_t nowUptimeMs, uint32_t thenUptimeMs) {
    // Unsigned subtraction keeps working across the millis() wrap.
    return nowEpoch - static_cast<int64_t>((nowUptimeMs - thenUptimeMs) / 1000);
}
}  // namespace

int dayKey(int64_t epoch, int resetHour) {
    const struct tm t = localTm(epoch - static_cast<int64_t>(resetHour) * 3600);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

int localSecondsOfDay(int64_t epoch) {
    const struct tm t = localTm(epoch);
    return t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
}

bool DayLog::resolve(int64_t nowEpoch, uint32_t nowUptimeMs) {
    if (nowEpoch <= 0) return false;
    bool changed = false;
    for (Walk& w : walks_) {
        if (w.startEpoch != 0 || !w.thisBoot) continue;
        w.startEpoch = epochAgo(nowEpoch, nowUptimeMs, w.startUptimeMs);
        w.endEpoch = epochAgo(nowEpoch, nowUptimeMs, w.endUptimeMs);
        changed = true;
    }
    return changed;
}

bool DayLog::counts(const Walk& w, bool clockSet, int todayKey) const {
    if (w.startEpoch == 0) return w.thisBoot;
    return clockSet && dayKey(w.startEpoch, resetHour_) == todayKey;
}

Today DayLog::today(int64_t nowEpoch, const Walk* live) const {
    Today t;
    const bool clockSet = nowEpoch > 0;
    const int todayKey = clockSet ? dayKey(nowEpoch, resetHour_) : 0;

    auto take = [&](const Walk& w) {
        if (!counts(w, clockSet, todayKey)) return;
        t.totalSeconds += w.walkSeconds;
        ++t.walkCount;
        if (w.startEpoch == 0 || !clockSet) return;
        StripSegment s;
        s.startSec = localSecondsOfDay(w.startEpoch);
        // A walk past midnight runs to the end of the strip's day.
        const bool sameDay = dayKey(w.endEpoch, 0) == dayKey(w.startEpoch, 0);
        s.endSec = sameDay ? localSecondsOfDay(w.endEpoch) : kSecondsPerDay;
        if (s.endSec < s.startSec) s.endSec = s.startSec;
        s.walkSeconds = w.walkSeconds;
        t.segments.push_back(s);
    };
    for (const Walk& w : walks_) take(w);
    if (live) take(*live);

    if (clockSet) t.nowSec = localSecondsOfDay(nowEpoch);
    return t;
}

void DayLog::prune(int64_t nowEpoch) {
    if (nowEpoch <= 0) return;
    std::vector<Walk> kept;
    for (const Walk& w : walks_) {
        if (w.startEpoch != 0 && w.endEpoch < nowEpoch - kKeepSeconds) continue;
        kept.push_back(w);
    }
    walks_.swap(kept);
}

std::string DayLog::toCsv(const Walk* live) const {
    std::string out = "start_utc,end_utc,walk_s\n";
    auto put = [&out](const Walk& w) {
        if (w.startEpoch == 0) return;
        char line[64];
        snprintf(line, sizeof(line), "%lld,%lld,%lu\n", static_cast<long long>(w.startEpoch),
                 static_cast<long long>(w.endEpoch), static_cast<unsigned long>(w.walkSeconds));
        out += line;
    };
    for (const Walk& w : walks_) put(w);
    if (live) put(*live);
    return out;
}

void DayLog::loadCsv(const std::string& csv) {
    walks_.clear();
    std::istringstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        long long start = 0, end = 0;
        unsigned long seconds = 0;
        char tail = 0;
        if (sscanf(line.c_str(), "%lld,%lld,%lu%c", &start, &end, &seconds, &tail) != 3) continue;
        if (start <= 0 || end < start) continue;
        Walk w;
        w.startEpoch = start;
        w.endEpoch = end;
        w.walkSeconds = static_cast<uint32_t>(seconds);
        walks_.push_back(w);
    }
}

}  // namespace day
