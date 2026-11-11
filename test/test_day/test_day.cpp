// Today's total and the day strip, in the London time zone
// (winter UTC+0, summer UTC+1).
#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "day_log.h"

using day::DayLog;
using day::Walk;

namespace {

int64_t utc(int y, int mo, int d, int h, int mi, int s = 0) {
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    return static_cast<int64_t>(timegm(&t));
}

Walk walk(int64_t start, int64_t end, uint32_t seconds) {
    Walk w;
    w.startEpoch = start;
    w.endEpoch = end;
    w.walkSeconds = seconds;
    return w;
}

constexpr int kHour = 3600;

}  // namespace

void setUp() {
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
}

void tearDown() {}

void test_day_key_turns_at_local_midnight_in_winter() {
    TEST_ASSERT_EQUAL(20261111, day::dayKey(utc(2026, 11, 11, 23, 59), 0));
    TEST_ASSERT_EQUAL(20261112, day::dayKey(utc(2026, 11, 12, 0, 1), 0));
}

void test_day_key_follows_summer_time() {
    // 23:30 UTC is already 00:30 of the next day in London in July.
    TEST_ASSERT_EQUAL(20260702, day::dayKey(utc(2026, 7, 1, 23, 30), 0));
    TEST_ASSERT_EQUAL(20260701, day::dayKey(utc(2026, 7, 1, 22, 59), 0));
}

void test_day_key_with_a_later_reset_hour() {
    // With the day starting at 04:00, 03:00 still belongs to the day before.
    TEST_ASSERT_EQUAL(20261111, day::dayKey(utc(2026, 11, 12, 3, 0), 4));
    TEST_ASSERT_EQUAL(20261112, day::dayKey(utc(2026, 11, 12, 4, 0), 4));
}

void test_local_seconds_of_day() {
    TEST_ASSERT_EQUAL(14 * kHour, day::localSecondsOfDay(utc(2026, 7, 1, 13, 0)));
    TEST_ASSERT_EQUAL(14 * kHour + 32 * 60, day::localSecondsOfDay(utc(2026, 11, 11, 14, 32)));
}

void test_today_sums_todays_walks_and_the_live_one() {
    DayLog log;
    log.add(walk(utc(2026, 11, 10, 18, 0), utc(2026, 11, 10, 18, 40), 2400));  // yesterday
    log.add(walk(utc(2026, 11, 11, 7, 10), utc(2026, 11, 11, 7, 40), 1750));
    log.add(walk(utc(2026, 11, 11, 9, 30), utc(2026, 11, 11, 9, 52), 1300));
    const Walk live = walk(utc(2026, 11, 11, 14, 0), utc(2026, 11, 11, 14, 32), 1934);

    const day::Today t = log.today(utc(2026, 11, 11, 14, 32), &live);
    TEST_ASSERT_EQUAL_UINT32(1750 + 1300 + 1934, t.totalSeconds);
    TEST_ASSERT_EQUAL(3, t.walkCount);
    TEST_ASSERT_EQUAL(3, t.segments.size());
    TEST_ASSERT_EQUAL(7 * kHour + 10 * 60, t.segments[0].startSec);
    TEST_ASSERT_EQUAL(7 * kHour + 40 * 60, t.segments[0].endSec);
    TEST_ASSERT_EQUAL(14 * kHour, t.segments[2].startSec);
    TEST_ASSERT_EQUAL(14 * kHour + 32 * 60, t.nowSec);
}

void test_total_starts_over_at_midnight() {
    DayLog log;
    log.add(walk(utc(2026, 11, 11, 20, 0), utc(2026, 11, 11, 20, 30), 1800));
    const day::Today t = log.today(utc(2026, 11, 12, 0, 5));
    TEST_ASSERT_EQUAL_UINT32(0, t.totalSeconds);
    TEST_ASSERT_EQUAL(0, t.walkCount);
    TEST_ASSERT_EQUAL(0, t.segments.size());
}

void test_walk_past_midnight_runs_to_the_end_of_the_strip() {
    DayLog log;
    log.add(walk(utc(2026, 11, 11, 23, 50), utc(2026, 11, 12, 0, 20), 1800));
    const day::Today t = log.today(utc(2026, 11, 11, 23, 59));
    TEST_ASSERT_EQUAL(1, t.segments.size());
    TEST_ASSERT_EQUAL(24 * kHour, t.segments[0].endSec);
}

void test_without_a_clock_only_this_boots_walks_count() {
    DayLog log;
    log.add(walk(utc(2026, 11, 11, 7, 10), utc(2026, 11, 11, 7, 40), 1750));  // from flash
    Walk undated;
    undated.thisBoot = true;
    undated.walkSeconds = 600;
    log.add(undated);
    Walk live;
    live.thisBoot = true;
    live.walkSeconds = 120;

    const day::Today t = log.today(0, &live);
    TEST_ASSERT_EQUAL_UINT32(720, t.totalSeconds);
    TEST_ASSERT_EQUAL(2, t.walkCount);
    TEST_ASSERT_EQUAL(0, t.segments.size());
    TEST_ASSERT_EQUAL(-1, t.nowSec);
}

void test_resolve_dates_walks_once_the_clock_is_set() {
    DayLog log;
    Walk w;
    w.thisBoot = true;
    w.startUptimeMs = 1000;
    w.endUptimeMs = 601000;
    w.walkSeconds = 600;
    log.add(w);

    const int64_t now = utc(2026, 11, 11, 10, 0);
    TEST_ASSERT_TRUE(log.resolve(now, 1201000));
    TEST_ASSERT_EQUAL_INT64(now - 1200, log.walks()[0].startEpoch);
    TEST_ASSERT_EQUAL_INT64(now - 600, log.walks()[0].endEpoch);
    TEST_ASSERT_FALSE(log.resolve(now, 1201000));

    const day::Today t = log.today(now);
    TEST_ASSERT_EQUAL(1, t.segments.size());
    TEST_ASSERT_EQUAL(9 * kHour + 40 * 60, t.segments[0].startSec);
}

void test_resolve_without_a_clock_does_nothing() {
    DayLog log;
    Walk w;
    w.thisBoot = true;
    log.add(w);
    TEST_ASSERT_FALSE(log.resolve(0, 5000));
}

void test_csv_round_trip_keeps_dated_walks_only() {
    DayLog log;
    log.add(walk(1762845000, 1762846800, 1750));
    Walk undated;
    undated.thisBoot = true;
    undated.walkSeconds = 300;
    log.add(undated);

    const std::string csv = log.toCsv();
    TEST_ASSERT_EQUAL_STRING("start_utc,end_utc,walk_s\n1762845000,1762846800,1750\n", csv.c_str());

    DayLog loaded;
    loaded.loadCsv(csv + "garbage\n5,1,10\n");
    TEST_ASSERT_EQUAL(1, loaded.walks().size());
    TEST_ASSERT_EQUAL_INT64(1762845000, loaded.walks()[0].startEpoch);
    TEST_ASSERT_EQUAL_UINT32(1750, loaded.walks()[0].walkSeconds);
    TEST_ASSERT_FALSE(loaded.walks()[0].thisBoot);
}

void test_csv_can_carry_the_walk_in_progress() {
    DayLog log;
    log.add(walk(1762845000, 1762846800, 1750));
    const Walk live = walk(1762860000, 1762861200, 1100);
    const std::string csv = log.toCsv(&live);
    TEST_ASSERT_EQUAL_STRING(
        "start_utc,end_utc,walk_s\n1762845000,1762846800,1750\n1762860000,1762861200,1100\n",
        csv.c_str());
    TEST_ASSERT_EQUAL(1, log.walks().size());  // saving does not add it
}

void test_prune_drops_walks_older_than_two_days() {
    const int64_t now = utc(2026, 11, 11, 12, 0);
    DayLog log;
    log.add(walk(now - 4 * 24 * kHour, now - 3 * 24 * kHour, 600));
    log.add(walk(now - 25 * kHour, now - 24 * kHour, 600));
    Walk undated;
    undated.thisBoot = true;
    log.add(undated);
    log.prune(now);
    TEST_ASSERT_EQUAL(2, log.walks().size());
    TEST_ASSERT_EQUAL_INT64(now - 25 * kHour, log.walks()[0].startEpoch);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_day_key_turns_at_local_midnight_in_winter);
    RUN_TEST(test_day_key_follows_summer_time);
    RUN_TEST(test_day_key_with_a_later_reset_hour);
    RUN_TEST(test_local_seconds_of_day);
    RUN_TEST(test_today_sums_todays_walks_and_the_live_one);
    RUN_TEST(test_total_starts_over_at_midnight);
    RUN_TEST(test_walk_past_midnight_runs_to_the_end_of_the_strip);
    RUN_TEST(test_without_a_clock_only_this_boots_walks_count);
    RUN_TEST(test_resolve_dates_walks_once_the_clock_is_set);
    RUN_TEST(test_resolve_without_a_clock_does_nothing);
    RUN_TEST(test_csv_round_trip_keeps_dated_walks_only);
    RUN_TEST(test_csv_can_carry_the_walk_in_progress);
    RUN_TEST(test_prune_drops_walks_older_than_two_days);
    return UNITY_END();
}
