// Walk session / segment logic, driven with synthetic 1 Hz samples.
#include <unity.h>

#include "walk_tracker.h"

using walk::Activity;
using walk::Events;
using walk::Sample;
using walk::Segment;
using walk::WalkTracker;

namespace {

Sample at(uint32_t second, float speed, float incline = 0.0f, bool fresh = true) {
    Sample s;
    s.uptimeMs = second * 1000u;
    s.fresh = fresh;
    s.speedKmh = speed;
    s.inclinePct = incline;
    return s;
}

// Feeds one sample per second for [from, to] and collects every event.
struct Run {
    WalkTracker tracker;
    std::vector<Segment> segments;
    int sessionsStarted = 0;
    std::vector<walk::Summary> summaries;

    void feed(const Sample& s) {
        Events ev = tracker.tick(s);
        if (ev.sessionStarted) ++sessionsStarted;
        for (const auto& seg : ev.closedSegments) segments.push_back(seg);
        if (ev.sessionEnded) summaries.push_back(*ev.sessionEnded);
    }

    template <typename F>
    void span(uint32_t from, uint32_t to, F make) {
        for (uint32_t t = from; t <= to; ++t) feed(make(t));
    }
};

}  // namespace

void test_idle_treadmill_starts_nothing() {
    Run r;
    r.span(0, 600, [](uint32_t t) { return at(t, 0.0f); });
    TEST_ASSERT_EQUAL(0, r.sessionsStarted);
    TEST_ASSERT_FALSE(r.tracker.inSession());
}

void test_walking_time_counts_seconds_while_moving() {
    Run r;
    r.span(0, 60, [](uint32_t t) { return at(t, 3.5f); });
    TEST_ASSERT_EQUAL(1, r.sessionsStarted);
    TEST_ASSERT_EQUAL_UINT32(60, r.tracker.walkSeconds());
}

void test_speed_ramp_makes_one_boundary_at_first_change() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.5f, 2.0f); });
    r.feed(at(101, 3.6f, 2.0f));
    r.feed(at(102, 3.7f, 2.0f));
    r.span(103, 200, [](uint32_t t) { return at(t, 3.8f, 2.0f); });

    TEST_ASSERT_EQUAL(1, r.segments.size());
    const Segment& s = r.segments[0];
    TEST_ASSERT_EQUAL(Activity::Walk, s.activity);
    TEST_ASSERT_EQUAL_UINT32(0, s.startMs);
    TEST_ASSERT_EQUAL_UINT32(101000, s.endMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, s.speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, s.inclinePct);
}

void test_short_blip_does_not_split_segment() {
    Run r;
    r.span(0, 49, [](uint32_t t) { return at(t, 3.5f); });
    r.feed(at(50, 3.6f));
    r.feed(at(51, 3.6f));
    r.span(52, 120, [](uint32_t t) { return at(t, 3.5f); });
    TEST_ASSERT_EQUAL(0, r.segments.size());
}

void test_incline_change_closes_segment() {
    Run r;
    r.span(0, 59, [](uint32_t t) { return at(t, 4.0f, 0.0f); });
    r.span(60, 120, [](uint32_t t) { return at(t, 4.0f, 6.0f); });
    TEST_ASSERT_EQUAL(1, r.segments.size());
    TEST_ASSERT_EQUAL_UINT32(60, r.segments[0].durationS());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.segments[0].inclinePct);
}

void test_short_pause_keeps_session_and_stops_clock() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.0f); });
    r.span(101, 200, [](uint32_t t) { return at(t, 0.0f); });
    r.span(201, 300, [](uint32_t t) { return at(t, 3.0f); });

    TEST_ASSERT_EQUAL(1, r.sessionsStarted);
    TEST_ASSERT_TRUE(r.tracker.inSession());
    TEST_ASSERT_EQUAL_UINT32(200, r.tracker.walkSeconds());

    TEST_ASSERT_EQUAL(2, r.segments.size());
    TEST_ASSERT_EQUAL(Activity::Walk, r.segments[0].activity);
    TEST_ASSERT_EQUAL_UINT32(101000, r.segments[0].endMs);
    TEST_ASSERT_EQUAL(Activity::Pause, r.segments[1].activity);
    TEST_ASSERT_EQUAL_UINT32(101000, r.segments[1].startMs);
    TEST_ASSERT_EQUAL_UINT32(201000, r.segments[1].endMs);
}

void test_long_stop_ends_session_at_last_movement() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.0f); });
    r.span(101, 450, [](uint32_t t) { return at(t, 0.0f); });

    TEST_ASSERT_FALSE(r.tracker.inSession());
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    const walk::Summary& sum = r.summaries[0];
    TEST_ASSERT_EQUAL_UINT32(0, sum.startMs);
    TEST_ASSERT_EQUAL_UINT32(101000, sum.endMs);
    TEST_ASSERT_EQUAL_UINT32(100000, sum.walkMs);
    TEST_ASSERT_EQUAL_UINT32(1, sum.segmentCount);
    // Trailing pause is not reported as a segment.
    TEST_ASSERT_EQUAL(1, r.segments.size());
    TEST_ASSERT_EQUAL(Activity::Walk, r.segments[0].activity);
    // Display keeps showing the finished walk's time.
    TEST_ASSERT_EQUAL_UINT32(100, r.tracker.walkSeconds());
}

void test_new_walk_after_session_end_starts_from_zero() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.0f); });
    r.span(101, 450, [](uint32_t t) { return at(t, 0.0f); });
    r.span(451, 461, [](uint32_t t) { return at(t, 3.0f); });
    TEST_ASSERT_EQUAL(2, r.sessionsStarted);
    TEST_ASSERT_EQUAL_UINT32(10, r.tracker.walkSeconds());
}

void test_distance_from_treadmill_counter() {
    Run r;
    r.span(0, 100, [](uint32_t t) {
        Sample s = at(t, 3.6f);
        s.treadmillDistanceM = 1000 + t;  // counter did not start at zero
        return s;
    });
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, r.tracker.distanceM());
}

void test_distance_integrated_without_counter() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.6f); });  // 1 m/s
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, r.tracker.distanceM());
}

void test_elevation_gain_from_incline() {
    Run r;
    r.span(0, 100, [](uint32_t t) {
        Sample s = at(t, 3.6f, 5.0f);
        s.treadmillDistanceM = t;
        return s;
    });
    r.span(101, 450, [](uint32_t t) {
        Sample s = at(t, 0.0f, 5.0f);
        s.treadmillDistanceM = 100;
        return s;
    });
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, r.summaries[0].elevationGainM);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.6f, r.summaries[0].averageSpeedKmh());
}

void test_link_drop_is_filled_from_treadmill_clock() {
    Run r;
    auto connected = [](uint32_t t) {
        Sample s = at(t, 3.0f);
        s.treadmillElapsedS = static_cast<uint16_t>(t);
        return s;
    };
    r.span(0, 100, connected);
    r.span(101, 130, [](uint32_t t) { return at(t, 0.0f, 0.0f, /*fresh=*/false); });
    r.span(131, 140, connected);

    // 100 s before the drop + 30 s while disconnected + 10 s after.
    TEST_ASSERT_UINT32_WITHIN(1, 140, r.tracker.walkSeconds());

    TEST_ASSERT_EQUAL(2, r.segments.size());
    TEST_ASSERT_EQUAL(Activity::NoData, r.segments[1].activity);
    TEST_ASSERT_EQUAL_UINT32(101000, r.segments[1].startMs);
    TEST_ASSERT_EQUAL_UINT32(131000, r.segments[1].endMs);
}

void test_pause_while_connected_is_not_backfilled() {
    // Even if the treadmill clock kept running during a pause, a pause with a live
    // link must not be added to walking time.
    Run r;
    r.span(0, 100, [](uint32_t t) {
        Sample s = at(t, 3.0f);
        s.treadmillElapsedS = static_cast<uint16_t>(t);
        return s;
    });
    r.span(101, 160, [](uint32_t t) {
        Sample s = at(t, 0.0f);
        s.treadmillElapsedS = static_cast<uint16_t>(t);
        return s;
    });
    r.span(161, 170, [](uint32_t t) {
        Sample s = at(t, 3.0f);
        s.treadmillElapsedS = static_cast<uint16_t>(t);
        return s;
    });
    TEST_ASSERT_EQUAL_UINT32(110, r.tracker.walkSeconds());
}

void test_heart_rate_summary_counts_walking_seconds_only() {
    Run r;
    auto withPulse = [](uint32_t t, float speed, uint16_t bpm) {
        Sample s = at(t, speed);
        s.heartRateBpm = bpm;
        return s;
    };
    r.span(0, 49, [&](uint32_t t) { return withPulse(t, 3.0f, 100); });
    r.span(50, 100, [&](uint32_t t) { return withPulse(t, 3.0f, 120); });
    // Higher pulse while standing after the walk must not count.
    r.span(101, 450, [&](uint32_t t) { return withPulse(t, 0.0f, 150); });

    TEST_ASSERT_EQUAL(1, r.summaries.size());
    const walk::Summary& sum = r.summaries[0];
    TEST_ASSERT_TRUE(sum.averageHeartRateBpm.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (50 * 100 + 51 * 120) / 101.0f, *sum.averageHeartRateBpm);
    TEST_ASSERT_EQUAL_UINT16(120, *sum.maxHeartRateBpm);
}

void test_segment_heart_rate_follows_segment_boundaries() {
    Run r;
    auto withPulse = [](uint32_t t, float speed, uint16_t bpm) {
        Sample s = at(t, speed);
        s.heartRateBpm = bpm;
        return s;
    };
    r.span(0, 59, [&](uint32_t t) { return withPulse(t, 3.0f, 100); });
    r.span(60, 119, [&](uint32_t t) { return withPulse(t, 4.0f, 130); });
    r.span(120, 450, [&](uint32_t t) { return withPulse(t, 0.0f, 90); });

    TEST_ASSERT_EQUAL(2, r.segments.size());
    // The seconds that confirmed the change belong to the new segment.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, *r.segments[0].averageHeartRateBpm);
    TEST_ASSERT_EQUAL_UINT16(100, *r.segments[0].maxHeartRateBpm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, *r.segments[1].averageHeartRateBpm);
    TEST_ASSERT_EQUAL_UINT16(130, *r.segments[1].maxHeartRateBpm);
}

void test_blip_pulse_stays_in_its_segment() {
    Run r;
    auto withPulse = [](uint32_t t, float speed, uint16_t bpm) {
        Sample s = at(t, speed);
        s.heartRateBpm = bpm;
        return s;
    };
    r.span(0, 59, [&](uint32_t t) { return withPulse(t, 3.0f, 100); });
    r.span(60, 61, [&](uint32_t t) { return withPulse(t, 3.1f, 160); });  // too short to split
    r.span(62, 100, [&](uint32_t t) { return withPulse(t, 3.0f, 100); });
    r.span(101, 450, [&](uint32_t t) { return withPulse(t, 0.0f, 90); });

    TEST_ASSERT_EQUAL(1, r.segments.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (99 * 100 + 2 * 160) / 101.0f,
                             *r.segments[0].averageHeartRateBpm);
    TEST_ASSERT_EQUAL_UINT16(160, *r.segments[0].maxHeartRateBpm);
}

void test_walk_without_strap_has_no_heart_rate() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.0f); });
    r.span(101, 450, [](uint32_t t) { return at(t, 0.0f); });
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_FALSE(r.summaries[0].averageHeartRateBpm.has_value());
    TEST_ASSERT_FALSE(r.summaries[0].maxHeartRateBpm.has_value());
    TEST_ASSERT_FALSE(r.segments[0].averageHeartRateBpm.has_value());
    TEST_ASSERT_FALSE(r.segments[0].maxHeartRateBpm.has_value());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_treadmill_starts_nothing);
    RUN_TEST(test_walking_time_counts_seconds_while_moving);
    RUN_TEST(test_speed_ramp_makes_one_boundary_at_first_change);
    RUN_TEST(test_short_blip_does_not_split_segment);
    RUN_TEST(test_incline_change_closes_segment);
    RUN_TEST(test_short_pause_keeps_session_and_stops_clock);
    RUN_TEST(test_long_stop_ends_session_at_last_movement);
    RUN_TEST(test_new_walk_after_session_end_starts_from_zero);
    RUN_TEST(test_distance_from_treadmill_counter);
    RUN_TEST(test_distance_integrated_without_counter);
    RUN_TEST(test_elevation_gain_from_incline);
    RUN_TEST(test_link_drop_is_filled_from_treadmill_clock);
    RUN_TEST(test_pause_while_connected_is_not_backfilled);
    RUN_TEST(test_heart_rate_summary_counts_walking_seconds_only);
    RUN_TEST(test_segment_heart_rate_follows_segment_boundaries);
    RUN_TEST(test_blip_pulse_stays_in_its_segment);
    RUN_TEST(test_walk_without_strap_has_no_heart_rate);
    return UNITY_END();
}
