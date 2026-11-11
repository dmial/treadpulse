// Heart Rate Measurement parsing. Payloads are built from the Heart Rate
// Service spec; flags 0x16 (contact detected, RR intervals) is what a worn
// Polar H10 sends.
#include <unity.h>

#include <vector>

#include "heart_rate.h"

namespace {

std::optional<heartrate::Measurement> parse(const std::vector<uint8_t>& bytes) {
    return heartrate::parseMeasurement(bytes.data(), bytes.size());
}

}  // namespace

void test_worn_strap_with_one_rr_interval() {
    // 72 bpm, RR 0x03E3 = 995/1024 s
    auto m = parse({0x16, 0x48, 0xE3, 0x03});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_UINT16(72, m->bpm);
    TEST_ASSERT_TRUE(*m->sensorContact);
    TEST_ASSERT_EQUAL(1, m->rrIntervalsMs.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 971.68f, m->rrIntervalsMs[0]);
    TEST_ASSERT_TRUE(m->valid());
}

void test_two_rr_intervals() {
    auto m = parse({0x16, 0x8C, 0x00, 0x04, 0x10, 0x04});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_UINT16(140, m->bpm);
    TEST_ASSERT_EQUAL(2, m->rrIntervalsMs.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, m->rrIntervalsMs[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1015.63f, m->rrIntervalsMs[1]);
}

void test_strap_off_skin_is_not_valid() {
    // Contact supported but not detected: the value is not a real pulse.
    auto m = parse({0x04, 0x48});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_FALSE(*m->sensorContact);
    TEST_ASSERT_FALSE(m->valid());
}

void test_zero_bpm_is_not_valid() {
    auto m = parse({0x00, 0x00});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_FALSE(m->sensorContact.has_value());
    TEST_ASSERT_FALSE(m->valid());
}

void test_sensor_without_contact_status_is_valid() {
    auto m = parse({0x00, 0x55});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_UINT16(85, m->bpm);
    TEST_ASSERT_TRUE(m->valid());
}

void test_uint16_value_and_energy() {
    // 16-bit value 300 bpm, energy 0x0102 = 258 kJ, then one RR interval
    auto m = parse({0x19, 0x2C, 0x01, 0x02, 0x01, 0x00, 0x04});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_UINT16(300, m->bpm);
    TEST_ASSERT_EQUAL_UINT16(258, *m->energyExpendedKj);
    TEST_ASSERT_EQUAL(1, m->rrIntervalsMs.size());
}

void test_truncated_payload_is_rejected() {
    TEST_ASSERT_FALSE(parse({}).has_value());
    TEST_ASSERT_FALSE(parse({0x16}).has_value());
    TEST_ASSERT_FALSE(parse({0x01, 0x2C}).has_value());        // 16-bit value cut
    TEST_ASSERT_FALSE(parse({0x08, 0x48, 0x01}).has_value());  // energy cut
}

void test_odd_trailing_rr_byte_is_ignored() {
    auto m = parse({0x16, 0x48, 0xE3, 0x03, 0x01});
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL(1, m->rrIntervalsMs.size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_worn_strap_with_one_rr_interval);
    RUN_TEST(test_two_rr_intervals);
    RUN_TEST(test_strap_off_skin_is_not_valid);
    RUN_TEST(test_zero_bpm_is_not_valid);
    RUN_TEST(test_sensor_without_contact_status_is_valid);
    RUN_TEST(test_uint16_value_and_energy);
    RUN_TEST(test_truncated_payload_is_rejected);
    RUN_TEST(test_odd_trailing_rr_byte_is_ignored);
    return UNITY_END();
}
