#include "stats.h"

#include <optional>

#include "day_log.h"

namespace stats {

namespace {

// Calendar days since 1970-01-01 <-> yyyymmdd (H. Hinnant's algorithms), so a
// day is one step whatever its length in hours (summer time).
int64_t daysFromKey(int key) {
    int64_t y = key / 10000;
    const unsigned m = key / 100 % 100, d = key % 100;
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

int keyFromDays(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
    return static_cast<int>(y * 10000 + m * 100 + d);
}

// `lastStart`: the start of the day's latest walk so far, whose goal is the day's.
void add(Day& d, int64_t& lastStart, const archive::Entry& e) {
    if (e.startEpoch >= lastStart) {
        lastStart = e.startEpoch;
        d.goalSeconds = e.goalSeconds;
    }
    d.walkSeconds += e.walkSeconds;
    d.steps += e.steps;
    d.pulseSeconds += e.pulseSeconds;
    for (size_t z = 0; z < d.zoneSeconds.size(); ++z) d.zoneSeconds[z] += e.zoneSeconds[z];
}

}  // namespace

bool Day::operator==(const Day& o) const {
    return key == o.key && tracked == o.tracked && walkSeconds == o.walkSeconds && steps == o.steps &&
           pulseSeconds == o.pulseSeconds && zoneSeconds == o.zoneSeconds && goalSeconds == o.goalSeconds;
}

bool Window::operator==(const Window& o) const {
    return days == o.days && firstKey == o.firstKey && streakDays == o.streakDays;
}

Window lastDays(const std::vector<archive::Entry>& entries, const archive::Entry* live,
                int64_t historyStart, int64_t now, int resetHour, uint32_t goalSeconds, int count) {
    Window w;
    const int todayKey = day::dayKey(now, resetHour);
    const int firstKey = historyStart > 0 ? day::dayKey(historyStart, resetHour) : todayKey;
    const int64_t today = daysFromKey(todayKey);

    for (int i = 0; i < count; ++i) {
        const int64_t n = today - (count - 1 - i);
        Day d;
        d.key = keyFromDays(n);
        d.dayOfMonth = d.key % 100;
        d.weekday = static_cast<int>(((n + 3) % 7 + 7) % 7);  // 1970-01-01 was a Thursday
        d.tracked = d.key >= firstKey;
        w.days.push_back(d);
    }

    std::vector<int64_t> lastStart(w.days.size(), 0);
    auto place = [&](const archive::Entry& e) {
        const int key = day::dayKey(e.startEpoch, resetHour);
        for (size_t i = 0; i < w.days.size(); ++i) {
            if (w.days[i].key == key) add(w.days[i], lastStart[i], e);
        }
    };
    for (const archive::Entry& e : entries) place(e);
    if (live) place(*live);

    uint32_t total = 0, steps = 0;
    for (const Day& d : w.days) {
        if (!d.tracked || d.key == todayKey) continue;  // today is not over yet
        ++w.fullDays;
        if (d.walkSeconds >= (d.goalSeconds > 0 ? d.goalSeconds : goalSeconds)) ++w.goalDays;
        total += d.walkSeconds;
        steps += d.steps;
        for (size_t z = 0; z < w.zoneSeconds.size(); ++z) w.zoneSeconds[z] += d.zoneSeconds[z];
    }
    if (w.fullDays > 0) {
        w.averageSeconds = total / w.fullDays;
        w.averageSteps = steps / w.fullDays;
    }
    if (firstKey > w.days.front().key) w.firstKey = firstKey;
    return w;
}

void DayTotals::addTo(Total& t, const archive::Entry& e) {
    t.walkSeconds += e.walkSeconds;
    if (e.startEpoch >= t.lastStart) {
        t.lastStart = e.startEpoch;
        t.goalSeconds = e.goalSeconds;
    }
}

void DayTotals::add(const archive::Entry& e) { addTo(days_[day::dayKey(e.startEpoch, resetHour_)], e); }

int DayTotals::streak(int64_t now, const archive::Entry* live, uint32_t goalSeconds) const {
    const int liveKey = live ? day::dayKey(live->startEpoch, resetHour_) : 0;
    // A day's total with the walk in progress, if it started that day; nullopt without walking.
    auto totalOf = [&](int key) -> std::optional<Total> {
        const auto it = days_.find(key);
        if (it == days_.end() && key != liveKey) return std::nullopt;
        Total t = it == days_.end() ? Total{} : it->second;
        if (key == liveKey) addTo(t, *live);
        return t;
    };
    auto onGoal = [&](const std::optional<Total>& t) {
        return t && t->walkSeconds >= (t->goalSeconds > 0 ? t->goalSeconds : goalSeconds);
    };

    const int64_t today = daysFromKey(day::dayKey(now, resetHour_));
    int run = onGoal(totalOf(keyFromDays(today))) ? 1 : 0;
    for (int64_t n = today - 1; onGoal(totalOf(keyFromDays(n))); --n) ++run;
    return run;
}

std::string shortDate(int key) {
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int m = key / 100 % 100;
    if (m < 1 || m > 12) return "";
    return std::string(kMonths[m - 1]) + " " + std::to_string(key % 100);
}

}  // namespace stats
