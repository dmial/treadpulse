// Per-second walk log: row format, and rebuilding a walk cut short by a power
// loss. Each test logs a walk the way the main loop does, cuts the log, replays
// it and compares with what the live tracker would have written.
#include <unity.h>

#include <cstdio>
#include <string>
#include <vector>

#include "walk_log.h"
#include "walk_tracker.h"

using walk::Activity;
using walk::Events;
using walk::LoggedRow;
using walk::ReplayResult;
using walk::Sample;
using walk::SampleRow;
using walk::Segment;
using walk::Summary;
using walk::WalkReplay;
using walk::WalkTracker;

namespace {

// Wall clock (UTC) of the test walks: second 0 is 2026-11-11T23:59:00Z, so a walk
// past second 60 crosses midnight.
std::string clockAt(uint32_t second) {
    char buf[32];
    if (second < 60) {
        snprintf(buf, sizeof(buf), "2026-11-11T23:59:%02uZ", second);
    } else {
        const uint32_t s = second - 60;
        snprintf(buf, sizeof(buf), "2026-11-12T%02u:%02u:%02uZ", s / 3600, s / 60 % 60, s % 60);
    }
    return buf;
}

// Drives a live tracker and writes the log exactly as src/main.cpp does.
struct LiveWalk {
    WalkTracker tracker;
    std::vector<std::string> log;
    std::vector<Segment> segments;
    std::optional<Summary> summary;
    bool clockSet = true;

    void feed(uint32_t second, const Sample& s) {
        const Events ev = tracker.tick(s);
        if (ev.sessionStarted) log.push_back(walk::kSamplesHeader);
        for (const Segment& seg : ev.closedSegments) segments.push_back(seg);
        if (tracker.inSession() || ev.sessionEnded) {
            SampleRow row;
            row.uptimeMs = s.uptimeMs;
            row.activity = tracker.lastActivity();
            row.walkSeconds = tracker.walkSeconds();
            row.speedKmh = s.speedKmh;
            row.inclinePct = s.inclinePct;
            row.distanceM = tracker.distanceM();
            row.treadmillDistanceM = s.treadmillDistanceM;
            row.treadmillElapsedS = s.treadmillElapsedS;
            row.heartRateBpm = s.heartRateBpm;
            log.push_back(walk::formatSampleRow(row, clockSet ? clockAt(second) : ""));
        }
        if (ev.sessionEnded) summary = ev.sessionEnded;
    }

    // What the live tracker would write if the walk ended at this moment.
    void endNow() {
        const Events ev = tracker.endSession();
        for (const Segment& seg : ev.closedSegments) segments.push_back(seg);
        if (ev.sessionEnded) summary = ev.sessionEnded;
    }
};

// A treadmill session: idle, then walking at the speed/incline `plan` gives,
// with the treadmill's own distance and clock counters and a pulse strap.
struct Treadmill {
    float distanceM = 0.0f;
    uint16_t elapsedS = 0;

    Sample at(uint32_t second, float speed, float incline, bool fresh = true) {
        if (speed > 0.0f) {
            distanceM += speed / 3.6f;
            ++elapsedS;
        }
        Sample s;
        s.uptimeMs = second * 1000u;
        s.fresh = fresh;
        s.speedKmh = speed;
        s.inclinePct = incline;
        s.treadmillDistanceM = static_cast<uint32_t>(distanceM);
        s.treadmillElapsedS = elapsedS;
        s.heartRateBpm = static_cast<uint16_t>(100 + second / 2);
        return s;
    }
};

ReplayResult replayLog(const std::vector<std::string>& log, WalkReplay& replay) {
    for (const std::string& line : log) {
        if (auto row = walk::parseSampleRow(line + "\r")) replay.add(*row);
    }
    return replay.finish();
}

void assertSameSegments(const std::vector<Segment>& expected, const std::vector<Segment>& actual) {
    TEST_ASSERT_EQUAL(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(expected[i].activity),
                          static_cast<int>(actual[i].activity));
        TEST_ASSERT_EQUAL_UINT32(expected[i].startMs, actual[i].startMs);
        TEST_ASSERT_EQUAL_UINT32(expected[i].endMs, actual[i].endMs);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i].speedKmh, actual[i].speedKmh);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i].inclinePct, actual[i].inclinePct);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i].distanceM, actual[i].distanceM);
        TEST_ASSERT_EQUAL(expected[i].maxHeartRateBpm.has_value(),
                          actual[i].maxHeartRateBpm.has_value());
        if (expected[i].maxHeartRateBpm) {
            TEST_ASSERT_EQUAL_UINT16(*expected[i].maxHeartRateBpm, *actual[i].maxHeartRateBpm);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, *expected[i].averageHeartRateBpm,
                                     *actual[i].averageHeartRateBpm);
        }
    }
}

void assertSameSummary(const Summary& expected, const Summary& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.startMs, actual.startMs);
    TEST_ASSERT_EQUAL_UINT32(expected.endMs, actual.endMs);
    TEST_ASSERT_EQUAL_UINT32(expected.walkMs, actual.walkMs);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected.distanceM, actual.distanceM);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected.maxSpeedKmh, actual.maxSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected.maxInclinePct, actual.maxInclinePct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected.elevationGainM, actual.elevationGainM);
    TEST_ASSERT_EQUAL_UINT32(expected.segmentCount, actual.segmentCount);
    TEST_ASSERT_TRUE(expected.maxHeartRateBpm.has_value());
    TEST_ASSERT_EQUAL_UINT16(*expected.maxHeartRateBpm, *actual.maxHeartRateBpm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, *expected.averageHeartRateBpm, *actual.averageHeartRateBpm);
}

}  // namespace

void test_row_round_trip() {
    SampleRow r;
    r.uptimeMs = 125'400;
    r.activity = Activity::Walk;
    r.walkSeconds = 98;
    r.speedKmh = 4.5f;
    r.inclinePct = 2.0f;
    r.distanceM = 123.4f;
    r.treadmillDistanceM = 2500;
    r.treadmillElapsedS = 1800;
    r.heartRateBpm = 131;

    const std::string line = walk::formatSampleRow(r, "2026-11-11T10:00:05Z");
    TEST_ASSERT_EQUAL_STRING("2026-11-11T10:00:05Z,125,walk,98,4.5,2.0,123.4,2500,1800,131",
                             line.c_str());

    const auto back = walk::parseSampleRow(line);
    TEST_ASSERT_TRUE(back.has_value());
    TEST_ASSERT_EQUAL_STRING("2026-11-11T10:00:05Z", back->time.c_str());
    TEST_ASSERT_EQUAL_UINT32(125'000, back->row.uptimeMs);  // the log keeps whole seconds
    TEST_ASSERT_EQUAL(static_cast<int>(Activity::Walk), static_cast<int>(back->row.activity));
    TEST_ASSERT_EQUAL_UINT32(98, back->row.walkSeconds);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, back->row.speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.4f, back->row.distanceM);
    TEST_ASSERT_EQUAL_UINT32(2500, *back->row.treadmillDistanceM);
    TEST_ASSERT_EQUAL_UINT16(1800, *back->row.treadmillElapsedS);
    TEST_ASSERT_EQUAL_UINT16(131, *back->row.heartRateBpm);
}

void test_row_without_clock_and_optional_values() {
    SampleRow r;
    r.uptimeMs = 7'000;
    r.activity = Activity::NoData;
    const std::string line = walk::formatSampleRow(r, "");
    TEST_ASSERT_EQUAL_STRING(",7,no_data,0,0.0,0.0,0.0,,,", line.c_str());

    const auto back = walk::parseSampleRow(line);
    TEST_ASSERT_TRUE(back.has_value());
    TEST_ASSERT_TRUE(back->time.empty());
    TEST_ASSERT_FALSE(back->row.treadmillDistanceM.has_value());
    TEST_ASSERT_FALSE(back->row.treadmillElapsedS.has_value());
    TEST_ASSERT_FALSE(back->row.heartRateBpm.has_value());
}

void test_header_and_damaged_lines_are_rejected() {
    TEST_ASSERT_FALSE(walk::parseSampleRow(walk::kSamplesHeader).has_value());
    TEST_ASSERT_FALSE(walk::parseSampleRow("").has_value());
    // Cut off mid-line by the power loss.
    TEST_ASSERT_FALSE(walk::parseSampleRow("2026-11-11T10:00:05Z,125,wa").has_value());
    // Right number of fields, but one number cut short into garbage.
    TEST_ASSERT_FALSE(
        walk::parseSampleRow("2026-11-11T10:00:05Z,125,walk,98,4.,2.0,1x3.4,2500,1800,131")
            .has_value());
    TEST_ASSERT_FALSE(
        walk::parseSampleRow("2026-11-11T10:0,125,walk,98,4.5,2.0,123.4,2500,1800,131")
            .has_value());
    // Zero bytes a damaged card can leave behind.
    TEST_ASSERT_FALSE(walk::parseSampleRow(std::string(40, '\0')).has_value());
}

void test_cut_while_walking_rebuilds_the_walk() {
    LiveWalk live;
    Treadmill mill;
    for (uint32_t t = 0; t <= 100; ++t) {
        float speed = 0.0f, incline = 0.0f;
        bool fresh = true;
        if (t >= 5 && t < 50) speed = 3.0f, incline = 1.0f;
        if (t >= 50) speed = 4.5f, incline = 2.0f;
        if (t >= 70 && t < 76) fresh = false;  // Bluetooth dropped for a few seconds
        live.feed(t, mill.at(t, speed, incline, fresh));
    }
    TEST_ASSERT_FALSE(live.summary.has_value());  // power cut: the walk never ended
    const std::vector<std::string> log = live.log;
    live.endNow();

    WalkReplay replay;
    const ReplayResult result = replayLog(log, replay);

    TEST_ASSERT_TRUE(result.summary.has_value());
    assertSameSegments(live.segments, result.segments);
    assertSameSummary(*live.summary, *result.summary);
    TEST_ASSERT_EQUAL_UINT32(100'000, result.summary->endMs);  // last logged movement
    TEST_ASSERT_EQUAL_STRING("2026-11-11T23:59:05Z", replay.timeAt(result.summary->startMs).c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-12T00:00:40Z", replay.timeAt(result.summary->endMs).c_str());
}

void test_cut_after_stopping_drops_the_trailing_pause() {
    // The usual case: belt stopped, then the power strip switched off before
    // the 5-minute idle timeout could close the walk.
    LiveWalk live;
    Treadmill mill;
    for (uint32_t t = 0; t <= 150; ++t) {
        const float speed = (t >= 5 && t <= 80) ? 3.5f : 0.0f;
        live.feed(t, mill.at(t, speed, 0.5f));
    }
    const std::vector<std::string> log = live.log;
    live.endNow();

    WalkReplay replay;
    const ReplayResult result = replayLog(log, replay);

    TEST_ASSERT_TRUE(result.summary.has_value());
    assertSameSegments(live.segments, result.segments);
    assertSameSummary(*live.summary, *result.summary);
    TEST_ASSERT_EQUAL_UINT32(81'000, result.summary->endMs);  // where the belt stopped
    TEST_ASSERT_EQUAL_UINT32(75, result.summary->walkMs / 1000);

    // Same result as if the board had stayed on until the idle timeout.
    LiveWalk full;
    Treadmill mill2;
    for (uint32_t t = 0; t <= 400 && !full.summary; ++t) {
        const float speed = (t >= 5 && t <= 80) ? 3.5f : 0.0f;
        full.feed(t, mill2.at(t, speed, 0.5f));
    }
    TEST_ASSERT_TRUE(full.summary.has_value());
    assertSameSegments(full.segments, result.segments);
    assertSameSummary(*full.summary, *result.summary);
}

void test_half_written_last_line_is_ignored() {
    LiveWalk live;
    Treadmill mill;
    for (uint32_t t = 0; t <= 40; ++t) live.feed(t, mill.at(t, t >= 5 ? 3.0f : 0.0f, 0.0f));
    std::vector<std::string> log = live.log;
    live.endNow();
    log.push_back(log.back().substr(0, 25));  // the power went out mid-write

    WalkReplay replay;
    const ReplayResult result = replayLog(log, replay);
    TEST_ASSERT_TRUE(result.summary.has_value());
    assertSameSummary(*live.summary, *result.summary);
}

void test_walk_without_clock_has_empty_times() {
    LiveWalk live;
    live.clockSet = false;
    Treadmill mill;
    for (uint32_t t = 0; t <= 30; ++t) live.feed(t, mill.at(t, t >= 5 ? 3.0f : 0.0f, 0.0f));

    WalkReplay replay;
    const ReplayResult result = replayLog(live.log, replay);
    TEST_ASSERT_TRUE(result.summary.has_value());
    TEST_ASSERT_EQUAL_STRING("", replay.timeAt(result.summary->startMs).c_str());
}

void test_log_without_walking_gives_nothing() {
    WalkReplay replay;
    const ReplayResult result = replayLog({walk::kSamplesHeader}, replay);
    TEST_ASSERT_FALSE(result.summary.has_value());
    TEST_ASSERT_EQUAL(0, result.segments.size());
}

void test_clock_jump_inside_a_walk_is_followed() {
    // The clock is corrected mid-walk and steps back an hour.
    WalkReplay replay;
    auto row = [](const char* time, uint32_t second) {
        LoggedRow r;
        r.time = time;
        r.row.uptimeMs = second * 1000u;
        r.row.activity = Activity::Walk;
        r.row.speedKmh = 3.0f;
        return r;
    };
    replay.add(row("2026-10-25T01:59:58Z", 10));
    replay.add(row("2026-10-25T01:59:59Z", 11));
    replay.add(row("2026-10-25T01:00:00Z", 12));
    replay.add(row("2026-10-25T01:00:01Z", 13));
    TEST_ASSERT_EQUAL_STRING("2026-10-25T01:59:59Z", replay.timeAt(11'000).c_str());
    TEST_ASSERT_EQUAL_STRING("2026-10-25T01:00:01Z", replay.timeAt(13'000).c_str());
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_row_round_trip);
    RUN_TEST(test_row_without_clock_and_optional_values);
    RUN_TEST(test_header_and_damaged_lines_are_rejected);
    RUN_TEST(test_cut_while_walking_rebuilds_the_walk);
    RUN_TEST(test_cut_after_stopping_drops_the_trailing_pause);
    RUN_TEST(test_half_written_last_line_is_ignored);
    RUN_TEST(test_walk_without_clock_has_empty_times);
    RUN_TEST(test_log_without_walking_gives_nothing);
    RUN_TEST(test_clock_jump_inside_a_walk_is_followed);
    return UNITY_END();
}
