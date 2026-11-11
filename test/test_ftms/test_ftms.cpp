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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_long_message_has_all_workout_fields);
    RUN_TEST(test_short_message_has_speed_and_elevation_only);
    RUN_TEST(test_slow_message_is_one_kmh);
    RUN_TEST(test_truncated_payload_is_rejected);
    RUN_TEST(test_negative_incline);
    RUN_TEST(test_state_keeps_incline_between_short_messages);
    return UNITY_END();
}
