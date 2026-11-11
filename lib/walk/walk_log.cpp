#include "walk_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace walk {

const char* const kSamplesHeader =
    "time,uptime_s,state,walk_time_s,speed_kmh,incline_pct,distance_m,"
    "treadmill_distance_m,treadmill_time_s,heart_rate_bpm";

namespace {

constexpr size_t kFieldCount = 10;

// Calendar arithmetic for "YYYY-MM-DDTHH:MM:SSZ" in UTC
// (H. Hinnant's days_from_civil / civil_from_days).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
}

// Value of `n` decimal digits at `pos`, or -1 if any of them is not a digit.
int digitsAt(const std::string& s, size_t pos, size_t n) {
    int v = 0;
    for (size_t i = pos; i < pos + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// Seconds since 1970-01-01T00:00:00Z.
std::optional<int64_t> parseUtcTime(const std::string& s) {
    if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':' || s[19] != 'Z') {
        return std::nullopt;
    }
    const int y = digitsAt(s, 0, 4), mo = digitsAt(s, 5, 2), d = digitsAt(s, 8, 2);
    const int h = digitsAt(s, 11, 2), mi = digitsAt(s, 14, 2), sec = digitsAt(s, 17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 ||
        mi > 59 || sec < 0 || sec > 60) {
        return std::nullopt;
    }
    return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
}

std::string formatUtcTime(int64_t t) {
    int64_t days = t / 86400;
    int64_t rest = t % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    int64_t y;
    unsigned m, d;
    civilFromDays(days, y, m, d);
    char buf[64];  // room for any int64 year, so the compiler sees no truncation
    snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ",
             static_cast<long long>(y), m, d, static_cast<long long>(rest / 3600),
             static_cast<long long>(rest / 60 % 60), static_cast<long long>(rest % 60));
    return buf;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
}

bool parseUint(const std::string& s, uint32_t max, uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > max) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = strtof(s.c_str(), &end);
    return end == s.c_str() + s.size();
}

// An empty field is a missing value; anything else must be a valid number.
template <typename T>
bool parseOptional(const std::string& s, uint32_t max, std::optional<T>& out) {
    if (s.empty()) {
        out.reset();
        return true;
    }
    uint32_t v;
    if (!parseUint(s, max, v)) return false;
    out = static_cast<T>(v);
    return true;
}

bool parseActivity(const std::string& s, Activity& out) {
    for (Activity a : {Activity::Walk, Activity::Pause, Activity::NoData}) {
        if (s == activityName(a)) {
            out = a;
            return true;
        }
    }
    return false;
}

}  // namespace

std::string formatSampleRow(const SampleRow& r, const std::string& time) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s,%lu,%s,%lu,%.1f,%.1f,%.1f,", time.c_str(),
             static_cast<unsigned long>(r.uptimeMs / 1000), activityName(r.activity),
             static_cast<unsigned long>(r.walkSeconds), r.speedKmh, r.inclinePct, r.distanceM);
    std::string line(buf);
    if (r.treadmillDistanceM) line += std::to_string(*r.treadmillDistanceM);
    line += ',';
    if (r.treadmillElapsedS) line += std::to_string(*r.treadmillElapsedS);
    line += ',';
    if (r.heartRateBpm) line += std::to_string(*r.heartRateBpm);
    return line;
}

std::optional<LoggedRow> parseSampleRow(const std::string& rawLine) {
    std::string line = rawLine;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();

    const std::vector<std::string> f = splitFields(line);
    if (f.size() != kFieldCount) return std::nullopt;

    LoggedRow out;
    out.time = f[0];
    if (!out.time.empty() && !parseUtcTime(out.time)) return std::nullopt;

    SampleRow& r = out.row;
    uint32_t uptimeS;
    if (!parseUint(f[1], UINT32_MAX / 1000, uptimeS)) return std::nullopt;
    r.uptimeMs = uptimeS * 1000;
    if (!parseActivity(f[2], r.activity)) return std::nullopt;
    if (!parseUint(f[3], UINT32_MAX / 1000, r.walkSeconds)) return std::nullopt;
    if (!parseFloat(f[4], r.speedKmh) || !parseFloat(f[5], r.inclinePct) ||
        !parseFloat(f[6], r.distanceM)) {
        return std::nullopt;
    }
    if (!parseOptional(f[7], UINT32_MAX, r.treadmillDistanceM) ||
        !parseOptional(f[8], UINT16_MAX, r.treadmillElapsedS) ||
        !parseOptional(f[9], UINT16_MAX, r.heartRateBpm)) {
        return std::nullopt;
    }
    return out;
}

void WalkReplay::add(const LoggedRow& logged) {
    noteClock(logged);
    if (summary_) return;  // the walk already ended; later rows are not part of it

    const SampleRow& r = logged.row;
    Sample s;
    s.uptimeMs = r.uptimeMs;
    s.fresh = r.activity != Activity::NoData;
    // The logged state is what the tracker decided live. Speed is rounded to
    // 0.1 km/h in the log, so a pause is replayed as a stopped belt.
    s.speedKmh = r.activity == Activity::Walk ? r.speedKmh : 0.0f;
    s.inclinePct = r.inclinePct;
    s.treadmillDistanceM = r.treadmillDistanceM;
    s.treadmillElapsedS = r.treadmillElapsedS;
    s.heartRateBpm = r.heartRateBpm;
    collect(tracker_.tick(s));
    last_ = r;
}

ReplayResult WalkReplay::finish() {
    if (!summary_) collect(tracker_.endSession());

    ReplayResult out{segments_, summary_};
    if (out.summary && last_) {
        // The live totals were logged every second; prefer them to the replay,
        // which only sees whole-second timestamps.
        out.summary->walkMs = last_->walkSeconds * 1000;
        out.summary->distanceM = last_->distanceM;
    }
    return out;
}

std::string WalkReplay::timeAt(uint32_t uptimeMs) const {
    if (clock_.empty()) return "";
    const uint32_t up = uptimeMs / 1000;
    const ClockRun* run = &clock_.front();
    for (const ClockRun& c : clock_) {
        if (c.uptimeS <= up) run = &c;
    }
    return formatUtcTime(static_cast<int64_t>(up) + run->offsetS);
}

void WalkReplay::noteClock(const LoggedRow& logged) {
    const std::optional<int64_t> utc = parseUtcTime(logged.time);
    if (!utc) return;
    const uint32_t up = logged.row.uptimeMs / 1000;
    const int64_t offset = *utc - static_cast<int64_t>(up);
    // Whole-second rounding makes the offset wobble by one; only a real jump
    // (the clock corrected by NTP) starts a new run.
    if (clock_.empty() || std::llabs(offset - clock_.back().offsetS) > 1) {
        clock_.push_back({up, offset});
    }
}

void WalkReplay::collect(const Events& ev) {
    for (const Segment& seg : ev.closedSegments) segments_.push_back(seg);
    if (ev.sessionEnded) summary_ = ev.sessionEnded;
}

}  // namespace walk
