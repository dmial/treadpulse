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

// With the treadmill's own distance and workout clock.
Sample withCounters(uint32_t second, float speed, uint32_t distanceM, uint16_t elapsedS) {
    Sample s = at(second, speed);
    s.treadmillDistanceM = distanceM;
    s.treadmillElapsedS = elapsedS;
    return s;
}

// Counts steps with the example step formula of user_config.example.h.
walk::Config withSteps() {
    walk::Config c;
    c.steps.base = 57.0f;
    c.steps.perKmh = 11.4f;
    return c;
}

// Feeds one sample per second for [from, to] and collects every event.
struct Run {
    explicit Run(walk::Config cfg = walk::Config()) : tracker(cfg) {}

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

// The main loop runs each tick a few ms late, by a different amount each time.
// The walk starts on a late tick, so many later ticks are less late than it:
// the shown timer must still step by exactly one second per tick.
void test_walking_time_steps_one_second_despite_tick_jitter() {
    Run r;
    const uint32_t lateMs[] = {6, 0, 3, 8, 1, 5, 0, 7, 2, 4};
    for (uint32_t t = 0; t <= 100; ++t) {
        Sample s = at(t, 3.5f);
        s.uptimeMs += lateMs[t % 10];
        r.feed(s);
        TEST_ASSERT_EQUAL_UINT32(t, r.tracker.walkSeconds());
    }
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

// A lone all-zero reading (the Merach W50 glitch, should it get past the
// link's filter) must not make the whole counter count twice.
void test_zero_counter_reading_does_not_add_distance_twice() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return withCounters(t, 3.6f, t, t); });  // 1 m/s
    r.feed(withCounters(101, 0.0f, 0, 0));
    r.span(102, 200, [](uint32_t t) { return withCounters(t, 3.6f, t, t); });
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, r.tracker.distanceM());
}

// A pause on the treadmill zeroes its counters; resuming brings them back.
void test_pause_zeroes_counters_and_resume_carries_on() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return withCounters(t, 3.6f, t, t); });
    r.span(101, 103, [](uint32_t t) { return withCounters(t, 0.0f, 100, 100); });
    r.span(104, 130, [](uint32_t t) { return withCounters(t, 0.0f, 0, 0); });
    r.span(131, 230, [](uint32_t t) { return withCounters(t, 3.6f, t - 31, t - 31); });

    TEST_ASSERT_EQUAL(1, r.sessionsStarted);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 199.0f, r.tracker.distanceM());
    TEST_ASSERT_EQUAL_UINT32(200, r.tracker.walkSeconds());
}

// The link drops during a pause, while the treadmill's clock reads 0: the
// pause must not be taken for walking once the clock comes back.
void test_link_drop_during_pause_is_not_walking() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return withCounters(t, 3.6f, t, t); });
    r.span(101, 103, [](uint32_t t) { return withCounters(t, 0.0f, 100, 100); });
    r.span(104, 110, [](uint32_t t) { return withCounters(t, 0.0f, 0, 0); });
    r.span(111, 170, [](uint32_t t) { return at(t, 0.0f, 0.0f, /*fresh=*/false); });
    r.span(171, 180, [](uint32_t t) { return withCounters(t, 3.6f, t - 71, t - 71); });

    TEST_ASSERT_UINT32_WITHIN(1, 110, r.tracker.walkSeconds());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 109.0f, r.tracker.distanceM());
}

// After a stop the treadmill starts a new workout from 0.
void test_new_workout_after_stop_counts_from_zero() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return withCounters(t, 3.6f, t, t); });
    r.span(101, 120, [](uint32_t t) { return withCounters(t, 0.0f, 0, 0); });
    r.span(121, 150, [](uint32_t t) { return withCounters(t, 3.6f, t - 120, t - 120); });
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 130.0f, r.tracker.distanceM());
}

void test_reported_stop_ends_walk_right_away() {
    Run r;
    r.span(0, 100, [](uint32_t t) { return at(t, 3.0f); });
    r.span(101, 104, [](uint32_t t) { return at(t, 0.0f); });
    Sample stop = at(105, 0.0f);
    stop.treadmillStopped = true;
    r.feed(stop);

    TEST_ASSERT_FALSE(r.tracker.inSession());
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(101000, r.summaries[0].endMs);
    TEST_ASSERT_EQUAL_UINT32(100000, r.summaries[0].walkMs);
    TEST_ASSERT_EQUAL(1, r.segments.size());
    TEST_ASSERT_EQUAL(Activity::Walk, r.segments[0].activity);

    // Walking again after a stop is a new walk.
    r.span(106, 110, [](uint32_t t) { return at(t, 3.0f); });
    TEST_ASSERT_EQUAL(2, r.sessionsStarted);
}

void test_stop_reported_just_before_the_belt_stops() {
    Run r;
    r.span(0, 99, [](uint32_t t) { return at(t, 3.0f); });
    Sample report = at(100, 3.0f);
    report.treadmillStopped = true;
    r.feed(report);
    TEST_ASSERT_TRUE(r.tracker.inSession());

    r.feed(at(101, 0.0f));
    TEST_ASSERT_FALSE(r.tracker.inSession());
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(101000, r.summaries[0].endMs);
    TEST_ASSERT_EQUAL_UINT32(100000, r.summaries[0].walkMs);
}

void test_stop_report_while_belt_keeps_moving_is_dropped() {
    Run r;
    r.span(0, 99, [](uint32_t t) { return at(t, 3.0f); });
    Sample report = at(100, 3.0f);
    report.treadmillStopped = true;
    r.feed(report);
    r.span(101, 200, [](uint32_t t) { return at(t, 3.0f); });
    r.span(201, 260, [](uint32_t t) { return at(t, 0.0f); });  // a pause, later on
    TEST_ASSERT_TRUE(r.tracker.inSession());
}

void test_walk_under_a_minute_is_not_worth_keeping() {
    walk::Summary sum;
    sum.walkMs = 59000;
    TEST_ASSERT_FALSE(sum.worthKeeping());
    sum.walkMs = 60000;
    TEST_ASSERT_TRUE(sum.worthKeeping());

    // A quick try of the buttons: 5 s at 1.0 km/h, then stop.
    Run r;
    r.span(0, 5, [](uint32_t t) { return at(t, 1.0f); });
    Sample stop = at(6, 0.0f);
    stop.treadmillStopped = true;
    r.feed(stop);
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_FALSE(r.summaries[0].worthKeeping());

    // Pauses do not count: 30 s of walking around a 3-minute pause.
    Run p;
    p.span(0, 15, [](uint32_t t) { return at(t, 3.0f); });
    p.span(16, 195, [](uint32_t t) { return at(t, 0.0f); });
    p.span(196, 210, [](uint32_t t) { return at(t, 3.0f); });
    p.span(211, 520, [](uint32_t t) { return at(t, 0.0f); });
    TEST_ASSERT_EQUAL(1, p.summaries.size());
    TEST_ASSERT_FALSE(p.summaries[0].worthKeeping());
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

// The zone borders of user_config.example.h.
walk::Config withZones() {
    walk::Config c;
    c.zones = pulse::Borders{95.0f, 115.0f, 135.0f, 150.0f, 165.0f, 185.0f};
    return c;
}

void test_pulse_seconds_split_by_zone() {
    Run r(withZones());
    auto withPulse = [](uint32_t t, float speed, std::optional<uint16_t> bpm) {
        Sample s = at(t, speed);
        s.heartRateBpm = bpm;
        return s;
    };
    // The first second starts the walk and adds no time, as for walkSeconds.
    r.span(0, 9, [&](uint32_t t) { return withPulse(t, 3.0f, 90); });             // rest
    r.span(10, 29, [&](uint32_t t) { return withPulse(t, 3.0f, 115); });          // zone 2: on a border
    r.span(30, 39, [&](uint32_t t) { return withPulse(t, 4.0f, std::nullopt); });  // strap off
    r.span(40, 49, [&](uint32_t t) { return withPulse(t, 5.0f, 190); });          // over zone 5
    // Standing after the walk does not count.
    r.span(50, 400, [&](uint32_t t) { return withPulse(t, 0.0f, 140); });

    TEST_ASSERT_EQUAL(1, r.summaries.size());
    const walk::Summary& sum = r.summaries[0];
    TEST_ASSERT_EQUAL_UINT32(49, sum.walkSeconds());
    TEST_ASSERT_EQUAL_UINT32(39, sum.pulseSeconds);
    TEST_ASSERT_EQUAL_UINT32(9, sum.zoneSeconds[0]);
    TEST_ASSERT_EQUAL_UINT32(0, sum.zoneSeconds[1]);
    TEST_ASSERT_EQUAL_UINT32(20, sum.zoneSeconds[2]);
    TEST_ASSERT_EQUAL_UINT32(0, sum.zoneSeconds[3]);
    TEST_ASSERT_EQUAL_UINT32(0, sum.zoneSeconds[4]);
    TEST_ASSERT_EQUAL_UINT32(10, sum.zoneSeconds[5]);
}

void test_zone_seconds_start_from_zero_each_walk() {
    Run r(withZones());
    auto withPulse = [](uint32_t t, float speed, uint16_t bpm) {
        Sample s = at(t, speed);
        s.heartRateBpm = bpm;
        return s;
    };
    r.span(0, 99, [&](uint32_t t) { return withPulse(t, 3.0f, 140); });
    r.span(100, 450, [&](uint32_t t) { return withPulse(t, 0.0f, 100); });
    r.span(451, 550, [&](uint32_t t) { return withPulse(t, 3.0f, 120); });
    r.span(551, 900, [&](uint32_t t) { return withPulse(t, 0.0f, 100); });

    TEST_ASSERT_EQUAL(2, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(99, r.summaries[0].zoneSeconds[3]);
    TEST_ASSERT_EQUAL_UINT32(0, r.summaries[1].zoneSeconds[3]);
    TEST_ASSERT_EQUAL_UINT32(99, r.summaries[1].zoneSeconds[2]);
    TEST_ASSERT_EQUAL_UINT32(99, r.summaries[1].pulseSeconds);
}

// Ticks late by more than a second count as at most two seconds of walking;
// the pulse time follows the same clock and never runs ahead of it.
void test_pulse_time_never_exceeds_walking_time() {
    Run r(withZones());
    for (uint32_t t = 0; t <= 100; ++t) {
        Sample s = at(t, 3.0f);
        if (t % 10 == 0) s.uptimeMs += 2500;  // a long blocking call
        s.heartRateBpm = 120;
        r.feed(s);
    }
    r.span(101, 450, [](uint32_t t) { return at(t, 0.0f); });
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(r.summaries[0].walkSeconds(), r.summaries[0].pulseSeconds);
    TEST_ASSERT_EQUAL_UINT32(r.summaries[0].walkSeconds(), r.summaries[0].zoneSeconds[2]);
}

void test_without_zones_pulse_seconds_still_count() {
    Run r;
    r.span(0, 99, [](uint32_t t) {
        Sample s = at(t, 3.0f);
        s.heartRateBpm = 130;
        return s;
    });
    r.span(100, 450, [](uint32_t t) { return at(t, 0.0f); });

    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(99, r.summaries[0].pulseSeconds);
    for (uint32_t z : r.summaries[0].zoneSeconds) TEST_ASSERT_EQUAL_UINT32(0, z);
}

void test_steps_follow_the_speed() {
    Run r(withSteps());
    r.span(0, 600, [](uint32_t t) { return at(t, 3.7f, 1.0f); });     // 99.2 a minute
    r.span(601, 1200, [](uint32_t t) { return at(t, 4.7f, 11.0f); });  // 110.6 a minute
    TEST_ASSERT_UINT32_WITHIN(1, 992 + 1106, r.tracker.steps());
    r.span(1201, 1600, [](uint32_t t) { return at(t, 0.0f); });
    TEST_ASSERT_EQUAL(1, r.summaries.size());
    TEST_ASSERT_EQUAL_UINT32(r.tracker.steps(), r.summaries[0].steps);

    // The next walk counts from zero.
    r.span(1601, 1661, [](uint32_t t) { return at(t, 4.0f); });  // 60 s after the first tick
    TEST_ASSERT_UINT32_WITHIN(1, 103, r.tracker.steps());
}

void test_pause_adds_no_steps() {
    Run r(withSteps());
    r.span(0, 300, [](uint32_t t) { return at(t, 4.0f); });
    r.span(301, 360, [](uint32_t t) { return at(t, 0.0f); });
    r.span(361, 660, [](uint32_t t) { return at(t, 4.0f); });
    // 600 s at 4.0 km/h: 102.6 steps a minute.
    TEST_ASSERT_UINT32_WITHIN(2, 1026, r.tracker.steps());
}

void test_link_drop_is_filled_with_steps_too() {
    Run r(withSteps());
    auto connected = [](uint32_t t) {
        Sample s = at(t, 3.0f);
        s.treadmillElapsedS = static_cast<uint16_t>(t);
        return s;
    };
    r.span(0, 100, connected);
    r.span(101, 130, [](uint32_t t) { return at(t, 0.0f, 0.0f, /*fresh=*/false); });
    r.span(131, 140, connected);
    // 140 s of walking at 3.0 km/h, 91.2 steps a minute, the gap included.
    TEST_ASSERT_UINT32_WITHIN(3, 213, r.tracker.steps());
}

void test_without_step_formula_no_steps() {
    Run r;
    r.span(0, 300, [](uint32_t t) { return at(t, 4.0f); });
    TEST_ASSERT_EQUAL_UINT32(0, r.tracker.steps());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_treadmill_starts_nothing);
    RUN_TEST(test_walking_time_counts_seconds_while_moving);
    RUN_TEST(test_walking_time_steps_one_second_despite_tick_jitter);
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
    RUN_TEST(test_zero_counter_reading_does_not_add_distance_twice);
    RUN_TEST(test_pause_zeroes_counters_and_resume_carries_on);
    RUN_TEST(test_link_drop_during_pause_is_not_walking);
    RUN_TEST(test_new_workout_after_stop_counts_from_zero);
    RUN_TEST(test_reported_stop_ends_walk_right_away);
    RUN_TEST(test_stop_reported_just_before_the_belt_stops);
    RUN_TEST(test_stop_report_while_belt_keeps_moving_is_dropped);
    RUN_TEST(test_walk_under_a_minute_is_not_worth_keeping);
    RUN_TEST(test_heart_rate_summary_counts_walking_seconds_only);
    RUN_TEST(test_segment_heart_rate_follows_segment_boundaries);
    RUN_TEST(test_blip_pulse_stays_in_its_segment);
    RUN_TEST(test_walk_without_strap_has_no_heart_rate);
    RUN_TEST(test_pulse_seconds_split_by_zone);
    RUN_TEST(test_zone_seconds_start_from_zero_each_walk);
    RUN_TEST(test_pulse_time_never_exceeds_walking_time);
    RUN_TEST(test_without_zones_pulse_seconds_still_count);
    RUN_TEST(test_steps_follow_the_speed);
    RUN_TEST(test_pause_adds_no_steps);
    RUN_TEST(test_link_drop_is_filled_with_steps_too);
    RUN_TEST(test_without_step_formula_no_steps);
    return UNITY_END();
}
