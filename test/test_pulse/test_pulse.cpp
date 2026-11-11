// Heart rate zones with uneven, fractional test borders
// (94.5 / 114.2 / 135.3 / 152.6 / 165.4 / 185.0).
#include <unity.h>

#include "pulse_zones.h"

using pulse::Hint;
using pulse::LineWatcher;

namespace {
const pulse::Borders kBorders = {94.5f, 114.2f, 135.3f, 152.6f, 165.4f, 185.0f};
constexpr float kCeiling = 135.3f;
constexpr float kInterval = 152.6f;
}  // namespace

void test_zone_of_whole_bpm() {
    TEST_ASSERT_EQUAL(0, pulse::zoneOf(kBorders, 94));
    TEST_ASSERT_EQUAL(1, pulse::zoneOf(kBorders, 95));
    TEST_ASSERT_EQUAL(1, pulse::zoneOf(kBorders, 114));
    TEST_ASSERT_EQUAL(2, pulse::zoneOf(kBorders, 115));
    TEST_ASSERT_EQUAL(2, pulse::zoneOf(kBorders, 135));
    TEST_ASSERT_EQUAL(3, pulse::zoneOf(kBorders, 136));
    TEST_ASSERT_EQUAL(3, pulse::zoneOf(kBorders, 152));
    TEST_ASSERT_EQUAL(4, pulse::zoneOf(kBorders, 153));
    TEST_ASSERT_EQUAL(5, pulse::zoneOf(kBorders, 166));
    TEST_ASSERT_EQUAL(5, pulse::zoneOf(kBorders, 195));  // above HRmax stays 5
}

void test_border_belongs_to_upper_zone() {
    TEST_ASSERT_EQUAL(1, pulse::zoneOf(kBorders, 94.5f));
    TEST_ASSERT_EQUAL(3, pulse::zoneOf(kBorders, 135.3f));
}

void test_scale_position() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pulse::scalePosition(kBorders, 80));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pulse::scalePosition(kBorders, 94.5f));
    // 128 is 65% into zone 2, the second of five segments.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, (1.0f + 13.8f / 21.1f) / 5.0f,
                             pulse::scalePosition(kBorders, 128));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, pulse::scalePosition(kBorders, 135.3f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pulse::scalePosition(kBorders, 185.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pulse::scalePosition(kBorders, 200));
}

void test_last_bpm_under_a_line() {
    TEST_ASSERT_EQUAL(135, pulse::lastBpmUnder(135.3f));
    TEST_ASSERT_EQUAL(152, pulse::lastBpmUnder(152.6f));
    TEST_ASSERT_EQUAL(134, pulse::lastBpmUnder(135.0f));
}

void test_hint_below_ceiling() {
    const Hint h = pulse::hintFor(128, kCeiling, kInterval);
    TEST_ASSERT_TRUE(h.kind == Hint::Kind::BelowCeiling);
    TEST_ASSERT_EQUAL(135, h.line);
    TEST_ASSERT_EQUAL(7, h.distance);
    TEST_ASSERT_EQUAL(0, pulse::hintFor(135, kCeiling, kInterval).distance);
}

void test_hint_between_lines_points_to_interval() {
    const Hint h = pulse::hintFor(136, kCeiling, kInterval);
    TEST_ASSERT_TRUE(h.kind == Hint::Kind::BelowInterval);
    TEST_ASSERT_EQUAL(152, h.line);
    TEST_ASSERT_EQUAL(16, h.distance);
}

void test_hint_above_interval() {
    const Hint h = pulse::hintFor(155, kCeiling, kInterval);
    TEST_ASSERT_TRUE(h.kind == Hint::Kind::AboveInterval);
    TEST_ASSERT_EQUAL(152, h.line);
    TEST_ASSERT_EQUAL(3, h.distance);
}

void test_watcher_reports_each_crossing_once() {
    LineWatcher w(kCeiling, kInterval);
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 130));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 135));
    TEST_ASSERT_EQUAL(LineWatcher::kCeiling, w.update(true, 136));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 138));
    TEST_ASSERT_EQUAL(LineWatcher::kInterval, w.update(true, 153));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 157));
}

void test_watcher_needs_a_real_drop_to_rearm() {
    LineWatcher w(kCeiling, kInterval);
    w.update(true, 127);
    TEST_ASSERT_EQUAL(LineWatcher::kCeiling, w.update(true, 136));
    // Wobbling around the line: 135.3 - 3 = 132.3, so 133 does not rearm.
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 133));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 136));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 132));
    TEST_ASSERT_EQUAL(LineWatcher::kCeiling, w.update(true, 137));
}

void test_watcher_first_reading_is_never_a_crossing() {
    LineWatcher w(kCeiling, kInterval);
    // Strap put on in the middle of an interval.
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 160));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 161));
}

void test_watcher_forgets_when_inactive() {
    LineWatcher w(kCeiling, kInterval);
    w.update(true, 130);
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(false, 150));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 150));  // first again
    TEST_ASSERT_EQUAL(LineWatcher::kInterval, w.update(true, 158));
}

void test_watcher_jump_over_both_lines_reports_the_higher() {
    LineWatcher w(kCeiling, kInterval);
    w.update(true, 130);
    TEST_ASSERT_EQUAL(LineWatcher::kInterval, w.update(true, 158));
    TEST_ASSERT_EQUAL(LineWatcher::kNone, w.update(true, 159));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zone_of_whole_bpm);
    RUN_TEST(test_border_belongs_to_upper_zone);
    RUN_TEST(test_scale_position);
    RUN_TEST(test_last_bpm_under_a_line);
    RUN_TEST(test_hint_below_ceiling);
    RUN_TEST(test_hint_between_lines_points_to_interval);
    RUN_TEST(test_hint_above_interval);
    RUN_TEST(test_watcher_reports_each_crossing_once);
    RUN_TEST(test_watcher_needs_a_real_drop_to_rearm);
    RUN_TEST(test_watcher_first_reading_is_never_a_crossing);
    RUN_TEST(test_watcher_forgets_when_inactive);
    RUN_TEST(test_watcher_jump_over_both_lines_reports_the_higher);
    return UNITY_END();
}
