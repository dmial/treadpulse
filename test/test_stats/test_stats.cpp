// The last two weeks for the statistics page, from history entries.
#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "archive.h"
#include "stats.h"
#include "walk_log.h"

using archive::Entry;

namespace {

int64_t utc(const char* iso) { return *walk::parseUtcTime(iso); }

Entry walkAt(const char* startIso, uint32_t walkS, uint32_t pulseS = 0,
             std::array<uint32_t, 6> zones = {}) {
    Entry e;
    e.startEpoch = utc(startIso);
    e.endEpoch = e.startEpoch + walkS + 60;
    e.walkSeconds = walkS;
    e.pulseSeconds = pulseS;
    e.zoneSeconds = zones;
    return e;
}

constexpr uint32_t kGoal = 120 * 60;

}  // namespace

void setUp() {
    setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0", 1);  // Lisbon
    tzset();
}

void tearDown() {}

void test_fourteen_days_end_today_with_weekdays() {
    const stats::Window w = stats::lastDays({}, nullptr, utc("2026-01-01T10:00:00Z"),
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(14, w.days.size());
    TEST_ASSERT_EQUAL(20261030, w.days.front().key);
    TEST_ASSERT_EQUAL(4, w.days.front().weekday);  // Friday
    TEST_ASSERT_EQUAL(20261112, w.days.back().key);
    TEST_ASSERT_EQUAL(3, w.days.back().weekday);  // Thursday
    TEST_ASSERT_EQUAL(12, w.days.back().dayOfMonth);
    TEST_ASSERT_EQUAL(20261102, w.days[3].key);
    TEST_ASSERT_EQUAL(0, w.days[3].weekday);  // Monday
    TEST_ASSERT_EQUAL(14, w.trackedDays);
    TEST_ASSERT_EQUAL(0, w.firstKey);  // the history covers the whole window
}

void test_days_before_the_history_are_not_tracked() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-11T09:00:00Z", 900),
        walkAt("2026-11-11T18:00:00Z", 2400, 2400, {60, 780, 1560, 0, 0, 0}),
        walkAt("2026-11-12T08:30:00Z", 1800, 1700, {1000, 700, 0, 0, 0, 0}),
        walkAt("2026-11-12T15:00:00Z", 1300),
    };
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(20261111, w.firstKey);
    TEST_ASSERT_EQUAL(2, w.trackedDays);
    TEST_ASSERT_FALSE(w.days[11].tracked);
    TEST_ASSERT_TRUE(w.days[12].tracked);
    TEST_ASSERT_EQUAL_UINT32(900 + 2400, w.days[12].walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(2400, w.days[12].pulseSeconds);
    TEST_ASSERT_EQUAL_UINT32(1800 + 1300, w.days[13].walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(1560, w.zoneSeconds[2]);
    TEST_ASSERT_EQUAL_UINT32(60 + 1000, w.zoneSeconds[0]);
    TEST_ASSERT_EQUAL_UINT32((900 + 2400 + 1800 + 1300) / 2, w.averageSeconds);
    TEST_ASSERT_EQUAL(0, w.goalDays);
}

void test_goal_days_and_average_count_days_without_walks() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-10T08:00:00Z", kGoal),       // on goal
        walkAt("2026-11-12T08:00:00Z", kGoal + 60),  // over it
    };
    const stats::Window w = stats::lastDays(walks, nullptr, utc("2026-11-10T08:00:00Z"),
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(3, w.trackedDays);  // the 11th, with no walk, counts too
    TEST_ASSERT_EQUAL(2, w.goalDays);
    TEST_ASSERT_EQUAL_UINT32((2 * kGoal + 60) / 3, w.averageSeconds);
}

void test_walk_in_progress_counts_today() {
    const Entry live = walkAt("2026-11-12T21:00:00Z", 600, 600, {0, 0, 600, 0, 0, 0});
    const stats::Window w = stats::lastDays({}, &live, utc("2026-11-11T10:00:00Z"),
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(600, w.days.back().walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(600, w.zoneSeconds[2]);
}

void test_walk_past_midnight_counts_on_its_first_day() {
    const std::vector<Entry> walks = {walkAt("2026-11-11T23:50:00Z", 1200)};
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(1200, w.days[12].walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(0, w.days[13].walkSeconds);
}

void test_walks_outside_the_window_are_left_out() {
    const std::vector<Entry> walks = {walkAt("2026-10-29T10:00:00Z", 3000),
                                      walkAt("2026-10-30T10:00:00Z", 1000)};
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(1000, w.days.front().walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(1000 / 14, w.averageSeconds);
}

// Summer time starts on 2026-03-29: that day has 23 hours and still gets its slot.
void test_short_day_of_summer_time_keeps_its_slot() {
    const stats::Window w = stats::lastDays({}, nullptr, 0, utc("2026-03-29T23:30:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(20260330, w.days.back().key);  // 00:30 local
    TEST_ASSERT_EQUAL(20260329, w.days[12].key);
    TEST_ASSERT_EQUAL(20260328, w.days[11].key);
    TEST_ASSERT_EQUAL(20260317, w.days.front().key);
}

void test_without_history_only_today_is_tracked() {
    const stats::Window w = stats::lastDays({}, nullptr, 0, utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(1, w.trackedDays);
    TEST_ASSERT_EQUAL(20261112, w.firstKey);
    TEST_ASSERT_TRUE(w.days.back().tracked);
    TEST_ASSERT_FALSE(w.days[12].tracked);
}

void test_short_date() {
    TEST_ASSERT_EQUAL_STRING("Nov 11", stats::shortDate(20261111).c_str());
    TEST_ASSERT_EQUAL_STRING("Jan 3", stats::shortDate(20270103).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fourteen_days_end_today_with_weekdays);
    RUN_TEST(test_days_before_the_history_are_not_tracked);
    RUN_TEST(test_goal_days_and_average_count_days_without_walks);
    RUN_TEST(test_walk_in_progress_counts_today);
    RUN_TEST(test_walk_past_midnight_counts_on_its_first_day);
    RUN_TEST(test_walks_outside_the_window_are_left_out);
    RUN_TEST(test_short_day_of_summer_time_keeps_its_slot);
    RUN_TEST(test_without_history_only_today_is_tracked);
    RUN_TEST(test_short_date);
    return UNITY_END();
}
