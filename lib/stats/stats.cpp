#include "stats.h"

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

void add(Day& d, const archive::Entry& e) {
    d.walkSeconds += e.walkSeconds;
    d.pulseSeconds += e.pulseSeconds;
    for (size_t z = 0; z < d.zoneSeconds.size(); ++z) d.zoneSeconds[z] += e.zoneSeconds[z];
}

}  // namespace

bool Day::operator==(const Day& o) const {
    return key == o.key && tracked == o.tracked && walkSeconds == o.walkSeconds &&
           pulseSeconds == o.pulseSeconds && zoneSeconds == o.zoneSeconds;
}

bool Window::operator==(const Window& o) const {
    return days == o.days && firstKey == o.firstKey;
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

    auto place = [&](const archive::Entry& e) {
        const int key = day::dayKey(e.startEpoch, resetHour);
        for (Day& d : w.days) {
            if (d.key == key) add(d, e);
        }
    };
    for (const archive::Entry& e : entries) place(e);
    if (live) place(*live);

    uint32_t total = 0;
    for (const Day& d : w.days) {
        if (!d.tracked) continue;
        ++w.trackedDays;
        if (d.walkSeconds >= goalSeconds) ++w.goalDays;
        total += d.walkSeconds;
        for (size_t z = 0; z < w.zoneSeconds.size(); ++z) w.zoneSeconds[z] += d.zoneSeconds[z];
    }
    if (w.trackedDays > 0) w.averageSeconds = total / w.trackedDays;
    if (firstKey > w.days.front().key) w.firstKey = firstKey;
    return w;
}

std::string shortDate(int key) {
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int m = key / 100 % 100;
    if (m < 1 || m > 12) return "";
    return std::string(kMonths[m - 1]) + " " + std::to_string(key % 100);
}

}  // namespace stats
