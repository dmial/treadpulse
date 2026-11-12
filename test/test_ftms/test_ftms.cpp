// FTMS Treadmill Data parsing, checked against real notifications captured from
// a Merach W50 treadmill with nRF Connect.
#include <unity.h>

#include <vector>

#include "ftms.h"

using ftms::parseTreadmillData;

namespace {

std::optional<ftms::TreadmillData> parse(const std::vector<uint8_t>& bytes) {
    return parseTreadmillData(bytes.data(), bytes.size());
}

// "8C05 AA00 5000 0028 0024 0006 0000 0000 0026 01" at 12:32:44
const std::vector<uint8_t> kLongMessage = {0x8C, 0x05, 0xAA, 0x00, 0x50, 0x00, 0x00,
                                           0x28, 0x00, 0x24, 0x00, 0x06, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x26, 0x01};

// "1020 AA00 2000 0000 0000 00" at 12:32:45
const std::vector<uint8_t> kShortMessage = {0x10, 0x20, 0xAA, 0x00, 0x20, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00};

// "1020 6400 0000 0000 0000 00" at 12:29:57
const std::vector<uint8_t> kSlowMessage = {0x10, 0x20, 0x64, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00};

// Recorded on 2026-11-12 while walking at 1.0 km/h: 230 m, 855 s and 857 s
// either side of the glitch, which replaced the 856 s pair with zeros.
const std::vector<uint8_t> kWalk855 = {0x8C, 0x05, 0x64, 0x00, 0xE6, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x57, 0x03};
const std::vector<uint8_t> kWalk857 = {0x8C, 0x05, 0x64, 0x00, 0xE6, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x59, 0x03};
const std::vector<uint8_t> kZeroLong = {0x8C, 0x05, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0,    0,    0, 0, 0, 0, 0, 0, 0};
const std::vector<uint8_t> kZeroShort = {0x10, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Stop pressed: speed 0 while distance and time hold (320 m, 1169 s); the
// zeros follow 3 s later.
const std::vector<uint8_t> kStopLong = {0x8C, 0x05, 0x00, 0x00, 0x40, 0x01, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x91, 0x04};

// Merges a message the way the board does; returns whether it was taken.
bool feed(ftms::TreadmillState& st, ftms::ZeroGlitchFilter& filter,
          const std::vector<uint8_t>& message, uint32_t ms) {
    const auto d = parse(message);
    TEST_ASSERT_TRUE(d.has_value());
    if (!filter.pass(st, *d, ms)) return false;
    st.merge(*d);
    return true;
}

}  // namespace

void test_long_message_has_all_workout_fields() {
    auto d = parse(kLongMessage);
    TEST_ASSERT_TRUE(d.has_value());
    TEST_ASSERT_EQUAL_HEX16(0x058C, d->flags);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.70f, *d->speedKmh);
    TEST_ASSERT_EQUAL_UINT32(80, *d->totalDistanceM);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, *d->inclinePct);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.6f, *d->rampAngleDeg);
    TEST_ASSERT_EQUAL_UINT16(6, *d->totalEnergyKcal);
    TEST_ASSERT_EQUAL_UINT8(0, *d->heartRateBpm);
    TEST_ASSERT_EQUAL_UINT16(294, *d->elapsedTimeS);
    TEST_ASSERT_FALSE(d->positiveElevationM.has_value());
}

void test_short_message_has_speed_and_elevation_only() {
    auto d = parse(kShortMessage);
    TEST_ASSERT_TRUE(d.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.70f, *d->speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.2f, *d->positiveElevationM);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, *d->negativeElevationM);
    TEST_ASSERT_FALSE(d->inclinePct.has_value());
    TEST_ASSERT_FALSE(d->totalDistanceM.has_value());
    TEST_ASSERT_FALSE(d->elapsedTimeS.has_value());
}

void test_slow_message_is_one_kmh() {
    auto d = parse(kSlowMessage);
    TEST_ASSERT_TRUE(d.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, *d->speedKmh);
}

void test_truncated_payload_is_rejected() {
    std::vector<uint8_t> cut(kLongMessage.begin(), kLongMessage.end() - 1);
    TEST_ASSERT_FALSE(parse(cut).has_value());
    TEST_ASSERT_FALSE(parse({}).has_value());
    TEST_ASSERT_FALSE(parse({0x8C}).has_value());
}

void test_negative_incline() {
    // Speed 3.00 km/h, incline -1.5 %, ramp -0.9 deg
    auto d = parse({0x08, 0x00, 0x2C, 0x01, 0xF1, 0xFF, 0xF7, 0xFF});
    TEST_ASSERT_TRUE(d.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, *d->speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.5f, *d->inclinePct);
}

void test_state_keeps_incline_between_short_messages() {
    ftms::TreadmillState st;
    st.merge(*parse(kLongMessage));
    st.merge(*parse(kShortMessage));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.70f, st.speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, st.inclinePct);
    TEST_ASSERT_EQUAL_UINT32(80, *st.totalDistanceM);
    TEST_ASSERT_EQUAL_UINT16(294, *st.elapsedTimeS);

    st.merge(*parse(kSlowMessage));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, st.speedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, st.inclinePct);
}

void test_zero_glitch_while_walking_is_dropped() {
    ftms::TreadmillState st;
    ftms::ZeroGlitchFilter filter;
    TEST_ASSERT_TRUE(feed(st, filter, kWalk855, 0));
    TEST_ASSERT_TRUE(feed(st, filter, kSlowMessage, 1));

    TEST_ASSERT_FALSE(feed(st, filter, kZeroLong, 800));
    TEST_ASSERT_FALSE(feed(st, filter, kZeroShort, 801));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, st.speedKmh);
    TEST_ASSERT_EQUAL_UINT32(230, *st.totalDistanceM);

    TEST_ASSERT_TRUE(feed(st, filter, kWalk857, 1600));
    TEST_ASSERT_TRUE(feed(st, filter, kSlowMessage, 1601));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, st.speedKmh);
    TEST_ASSERT_EQUAL_UINT32(230, *st.totalDistanceM);
    TEST_ASSERT_EQUAL_UINT16(857, *st.elapsedTimeS);
}

void test_real_stop_is_taken_at_once() {
    ftms::TreadmillState st;
    ftms::ZeroGlitchFilter filter;
    feed(st, filter, kWalk855, 0);
    TEST_ASSERT_TRUE(feed(st, filter, kStopLong, 800));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, st.speedKmh);
    TEST_ASSERT_EQUAL_UINT32(320, *st.totalDistanceM);
    TEST_ASSERT_TRUE(feed(st, filter, kZeroShort, 801));
    // Seconds later the totals go to zero too, with the belt already stopped.
    TEST_ASSERT_TRUE(feed(st, filter, kZeroLong, 4000));
    TEST_ASSERT_EQUAL_UINT32(0, *st.totalDistanceM);
}

void test_zeros_that_keep_coming_are_a_stop() {
    ftms::TreadmillState st;
    ftms::ZeroGlitchFilter filter;
    feed(st, filter, kWalk855, 0);
    TEST_ASSERT_FALSE(feed(st, filter, kZeroLong, 800));
    TEST_ASSERT_FALSE(feed(st, filter, kZeroShort, 801));
    TEST_ASSERT_FALSE(feed(st, filter, kZeroLong, 1600));
    TEST_ASSERT_TRUE(feed(st, filter, kZeroLong, 2800));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, st.speedKmh);
    TEST_ASSERT_TRUE(feed(st, filter, kZeroShort, 2801));
}

// The link dropped mid-walk and the treadmill was stopped meanwhile: its
// zeros after the reconnect are not a glitch.
void test_zeros_after_a_gap_in_the_link_are_taken() {
    ftms::TreadmillState st;
    ftms::ZeroGlitchFilter filter;
    feed(st, filter, kWalk855, 0);
    TEST_ASSERT_TRUE(feed(st, filter, kZeroLong, 10000));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, st.speedKmh);
}

void test_machine_status_events() {
    auto status = [](const std::vector<uint8_t>& b) {
        return ftms::parseMachineStatus(b.data(), b.size());
    };
    // Recorded from the Merach W50: stop, pause, start / resume.
    TEST_ASSERT_TRUE(status({0x02, 0x01}) == ftms::MachineEvent::Stopped);
    TEST_ASSERT_TRUE(status({0x02, 0x02}) == ftms::MachineEvent::Paused);
    TEST_ASSERT_TRUE(status({0x04}) == ftms::MachineEvent::Started);
    TEST_ASSERT_TRUE(status({0x03}) == ftms::MachineEvent::SafetyStop);
    // Stop or pause without saying which: not acted on.
    TEST_ASSERT_TRUE(status({0x02}) == ftms::MachineEvent::Other);
    TEST_ASSERT_TRUE(status({0x05, 0x64, 0x00}) == ftms::MachineEvent::Other);
    TEST_ASSERT_FALSE(status({}).has_value());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_long_message_has_all_workout_fields);
    RUN_TEST(test_short_message_has_speed_and_elevation_only);
    RUN_TEST(test_slow_message_is_one_kmh);
    RUN_TEST(test_truncated_payload_is_rejected);
    RUN_TEST(test_negative_incline);
    RUN_TEST(test_state_keeps_incline_between_short_messages);
    RUN_TEST(test_zero_glitch_while_walking_is_dropped);
    RUN_TEST(test_real_stop_is_taken_at_once);
    RUN_TEST(test_zeros_that_keep_coming_are_a_stop);
    RUN_TEST(test_zeros_after_a_gap_in_the_link_are_taken);
    RUN_TEST(test_machine_status_events);
    return UNITY_END();
}
