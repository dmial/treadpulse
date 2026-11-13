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

constexpr uint32_t kGoal = 120 * 60;

Entry walkAt(const char* startIso, uint32_t walkS, uint32_t pulseS = 0,
             std::array<uint32_t, 6> zones = {}, uint32_t goalS = kGoal) {
    Entry e;
    e.goalSeconds = goalS;
    e.startEpoch = utc(startIso);
    e.endEpoch = e.startEpoch + walkS + 60;
    e.walkSeconds = walkS;
    e.pulseSeconds = pulseS;
    e.zoneSeconds = zones;
    return e;
}

// A walk of `walkS` at 10:00 UTC `daysAgo` days before 2026-11-12, by `goalS`.
Entry walkDaysAgo(int daysAgo, uint32_t walkS, uint32_t goalS = kGoal) {
    Entry e = walkAt("2026-11-12T10:00:00Z", walkS, 0, {}, goalS);
    e.startEpoch -= static_cast<int64_t>(daysAgo) * 24 * 3600;
    e.endEpoch -= static_cast<int64_t>(daysAgo) * 24 * 3600;
    return e;
}

int streakOf(const std::vector<Entry>& walks, const char* nowIso, const Entry* live = nullptr) {
    stats::DayTotals totals(0);
    for (const Entry& e : walks) totals.add(e);
    return totals.streak(utc(nowIso), live, kGoal);
}

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
    TEST_ASSERT_EQUAL(13, w.fullDays);  // today is not over yet
    TEST_ASSERT_EQUAL(0, w.firstKey);   // the history covers the whole window
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
    TEST_ASSERT_EQUAL(1, w.fullDays);  // the 11th; the 12th is today
    TEST_ASSERT_FALSE(w.days[11].tracked);
    TEST_ASSERT_TRUE(w.days[12].tracked);
    TEST_ASSERT_EQUAL_UINT32(900 + 2400, w.days[12].walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(2400, w.days[12].pulseSeconds);
    TEST_ASSERT_EQUAL_UINT32(1800 + 1300, w.days[13].walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(1560, w.zoneSeconds[2]);
    TEST_ASSERT_EQUAL_UINT32(60, w.zoneSeconds[0]);
    TEST_ASSERT_EQUAL_UINT32(900 + 2400, w.averageSeconds);
    TEST_ASSERT_EQUAL(0, w.goalDays);
}

void test_goal_days_and_average_count_days_without_walks() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-09T08:00:00Z", kGoal),       // on goal
        walkAt("2026-11-11T08:00:00Z", kGoal + 60),  // over it
    };
    const stats::Window w = stats::lastDays(walks, nullptr, utc("2026-11-09T08:00:00Z"),
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(3, w.fullDays);  // the 10th, with no walk, counts too
    TEST_ASSERT_EQUAL(2, w.goalDays);
    TEST_ASSERT_EQUAL(3, w.goalOf);
    TEST_ASSERT_EQUAL_UINT32((2 * kGoal + 60) / 3, w.averageSeconds);
}

void test_walk_in_progress_shows_on_todays_bar() {
    const Entry live = walkAt("2026-11-12T21:00:00Z", 600, 600, {0, 0, 600, 0, 0, 0});
    const stats::Window w = stats::lastDays({}, &live, utc("2026-11-11T10:00:00Z"),
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(600, w.days.back().walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(600, w.days.back().zoneSeconds[2]);
}

// Today shows on its bar but stays out of the averages until it is over: a
// morning without a walk yet would pull them down. On goal, it joins the goal
// days already, as in the days in a row.
void test_today_stays_out_of_the_averages() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-11T10:00:00Z", 3000, 3000, {0, 1200, 1800, 0, 0, 0}),
        walkAt("2026-11-12T10:00:00Z", kGoal, kGoal, {0, 0, kGoal, 0, 0, 0}),
    };
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(kGoal, w.days.back().walkSeconds);
    TEST_ASSERT_EQUAL(1, w.fullDays);
    TEST_ASSERT_EQUAL(1, w.goalDays);  // today, on goal
    TEST_ASSERT_EQUAL(2, w.goalOf);
    TEST_ASSERT_EQUAL_UINT32(3000, w.averageSeconds);
    TEST_ASSERT_EQUAL_UINT32(1800, w.zoneSeconds[2]);
}

// Under its goal, today stays out of the goal days: it is not over yet.
void test_today_joins_the_goal_days_once_on_goal() {
    const std::vector<Entry> walks = {walkAt("2026-11-11T10:00:00Z", kGoal),
                                      walkAt("2026-11-12T08:00:00Z", kGoal - 600)};
    const Entry live = walkAt("2026-11-12T21:00:00Z", 600);
    const stats::Window before = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                                 utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(1, before.goalDays);
    TEST_ASSERT_EQUAL(1, before.goalOf);
    const stats::Window after = stats::lastDays(walks, &live, walks[0].startEpoch,
                                                utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(2, after.goalDays);  // the walk in progress got today there
    TEST_ASSERT_EQUAL(2, after.goalOf);
    TEST_ASSERT_EQUAL(1, after.fullDays);
    TEST_ASSERT_EQUAL_UINT32(kGoal, after.averageSeconds);
}

// The day ends at the reset hour: at 02:00 with a 4 o'clock reset, yesterday
// is still today.
void test_today_ends_at_the_reset_hour() {
    const std::vector<Entry> walks = {walkAt("2026-11-11T10:00:00Z", 3000)};
    const stats::Window before = stats::lastDays(walks, nullptr, utc("2026-11-10T10:00:00Z"),
                                                 utc("2026-11-12T02:00:00Z"), 4, kGoal);
    TEST_ASSERT_EQUAL(1, before.fullDays);  // the 10th
    TEST_ASSERT_EQUAL_UINT32(0, before.averageSeconds);
    const stats::Window after = stats::lastDays(walks, nullptr, utc("2026-11-10T10:00:00Z"),
                                                utc("2026-11-12T05:00:00Z"), 4, kGoal);
    TEST_ASSERT_EQUAL(2, after.fullDays);
    TEST_ASSERT_EQUAL_UINT32(1500, after.averageSeconds);
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
    TEST_ASSERT_EQUAL_UINT32(1000 / 13, w.averageSeconds);
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
    TEST_ASSERT_EQUAL(0, w.fullDays);  // no totals before the first full day
    TEST_ASSERT_EQUAL_UINT32(0, w.averageSeconds);
    TEST_ASSERT_EQUAL(20261112, w.firstKey);
    TEST_ASSERT_TRUE(w.days.back().tracked);
    TEST_ASSERT_FALSE(w.days[12].tracked);
}

// A day is on goal by the goal it was walked under, not today's.
void test_goal_days_go_by_each_days_own_goal() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-10T10:00:00Z", 100 * 60, 0, {}, 90 * 60),   // on its 90 min goal
        walkAt("2026-11-11T10:00:00Z", 130 * 60, 0, {}, 150 * 60),  // under its 150
    };
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(2, w.fullDays);
    TEST_ASSERT_EQUAL(1, w.goalDays);
}

// The goal changed during the day: the day goes by its last walk's goal.
void test_days_goal_is_its_last_walks() {
    const std::vector<Entry> walks = {
        walkAt("2026-11-11T16:00:00Z", 60 * 60, 0, {}, 150 * 60),
        walkAt("2026-11-11T09:00:00Z", 40 * 60, 0, {}, 90 * 60),  // earlier, listed later
    };
    const stats::Window w = stats::lastDays(walks, nullptr, walks[1].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(150 * 60, w.days[12].goalSeconds);
    TEST_ASSERT_EQUAL(0, w.goalDays);
}

// Walks kept before the history had the goal go by the goal of now.
void test_walk_without_a_goal_goes_by_todays() {
    const std::vector<Entry> walks = {walkAt("2026-11-11T10:00:00Z", kGoal, 0, {}, 0)};
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL(1, w.goalDays);
    TEST_ASSERT_EQUAL(1, streakOf(walks, "2026-11-12T21:40:00Z"));
}

void test_average_steps_count_full_days() {
    std::vector<Entry> walks = {walkAt("2026-11-10T10:00:00Z", 3000), walkAt("2026-11-11T10:00:00Z", 3000),
                                walkAt("2026-11-12T10:00:00Z", 3000)};
    walks[0].steps = 9000;
    walks[1].steps = 6000;
    walks[2].steps = 50000;  // today: not in the average
    const stats::Window w = stats::lastDays(walks, nullptr, walks[0].startEpoch,
                                            utc("2026-11-12T21:40:00Z"), 0, kGoal);
    TEST_ASSERT_EQUAL_UINT32(7500, w.averageSteps);
}

void test_streak_counts_days_on_goal_up_to_yesterday() {
    const std::vector<Entry> walks = {walkDaysAgo(4, kGoal), walkDaysAgo(3, kGoal - 60),  // short
                                      walkDaysAgo(2, kGoal), walkDaysAgo(1, kGoal + 600)};
    TEST_ASSERT_EQUAL(2, streakOf(walks, "2026-11-12T21:40:00Z"));
}

// Today joins the run once it is on goal and does not break it before.
void test_streak_takes_today_once_on_goal() {
    std::vector<Entry> walks = {walkDaysAgo(1, kGoal), walkDaysAgo(0, kGoal - 60)};
    TEST_ASSERT_EQUAL(1, streakOf(walks, "2026-11-12T21:40:00Z"));
    walks.push_back(walkDaysAgo(0, 60));
    walks.back().startEpoch += 3600;
    TEST_ASSERT_EQUAL(2, streakOf(walks, "2026-11-12T21:40:00Z"));
}

void test_streak_takes_the_walk_in_progress() {
    const std::vector<Entry> walks = {walkDaysAgo(1, kGoal), walkDaysAgo(0, 100 * 60)};
    Entry live = walkAt("2026-11-12T20:00:00Z", 20 * 60);
    TEST_ASSERT_EQUAL(2, streakOf(walks, "2026-11-12T21:40:00Z", &live));
    live.walkSeconds = 19 * 60;
    TEST_ASSERT_EQUAL(1, streakOf(walks, "2026-11-12T21:40:00Z", &live));
}

void test_streak_ends_on_a_day_without_walking() {
    const std::vector<Entry> walks = {walkDaysAgo(3, kGoal), walkDaysAgo(1, kGoal)};
    TEST_ASSERT_EQUAL(1, streakOf(walks, "2026-11-12T21:40:00Z"));
    TEST_ASSERT_EQUAL(0, streakOf({}, "2026-11-12T21:40:00Z"));
}

// The run goes back past the two weeks of the page, across the summer time change.
void test_streak_goes_back_past_the_window() {
    std::vector<Entry> walks;
    for (int d = 1; d <= 240; ++d) walks.push_back(walkDaysAgo(d, kGoal));
    TEST_ASSERT_EQUAL(240, streakOf(walks, "2026-11-12T21:40:00Z"));
}

// A higher goal later does not shorten the run behind it.
void test_streak_goes_by_each_days_own_goal() {
    const std::vector<Entry> walks = {walkDaysAgo(2, 90 * 60, 90 * 60), walkDaysAgo(1, 150 * 60, 150 * 60)};
    TEST_ASSERT_EQUAL(2, streakOf(walks, "2026-11-12T21:40:00Z"));
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
    RUN_TEST(test_walk_in_progress_shows_on_todays_bar);
    RUN_TEST(test_today_stays_out_of_the_averages);
    RUN_TEST(test_today_joins_the_goal_days_once_on_goal);
    RUN_TEST(test_today_ends_at_the_reset_hour);
    RUN_TEST(test_walk_past_midnight_counts_on_its_first_day);
    RUN_TEST(test_walks_outside_the_window_are_left_out);
    RUN_TEST(test_short_day_of_summer_time_keeps_its_slot);
    RUN_TEST(test_without_history_only_today_is_tracked);
    RUN_TEST(test_goal_days_go_by_each_days_own_goal);
    RUN_TEST(test_days_goal_is_its_last_walks);
    RUN_TEST(test_walk_without_a_goal_goes_by_todays);
    RUN_TEST(test_average_steps_count_full_days);
    RUN_TEST(test_streak_counts_days_on_goal_up_to_yesterday);
    RUN_TEST(test_streak_takes_today_once_on_goal);
    RUN_TEST(test_streak_takes_the_walk_in_progress);
    RUN_TEST(test_streak_ends_on_a_day_without_walking);
    RUN_TEST(test_streak_goes_back_past_the_window);
    RUN_TEST(test_streak_goes_by_each_days_own_goal);
    RUN_TEST(test_short_date);
    return UNITY_END();
}
