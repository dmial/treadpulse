// The pulse level over the last minutes: average and the band of its middle values.
#include <unity.h>

#include <vector>

#include "pulse_level.h"

void test_mean_and_band_of_steady_pulse() {
    // 120..130 once each: the middle 80 % spans 121..129.
    std::vector<float> s;
    for (int b = 120; b <= 130; ++b) s.push_back(static_cast<float>(b));
    const pulse::Level l = pulse::levelOf(s, 0, static_cast<int>(s.size()), 1);
    TEST_ASSERT_TRUE(l.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 125.0f, l.mean);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 121.0f, l.low);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 129.0f, l.high);
}

void test_band_between_neighbours() {
    // 4 samples: the 10th percentile sits 0.3 of the way from the 1st to the 2nd.
    const std::vector<float> s = {120, 130, 130, 140};
    const pulse::Level l = pulse::levelOf(s, 0, 4, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.0f, l.low);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 137.0f, l.high);
}

void test_short_spike_does_not_widen_the_band() {
    std::vector<float> s(30, 128.0f);
    s[10] = 150.0f;  // one sample out of 30
    const pulse::Level l = pulse::levelOf(s, 0, 30, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 128.0f, l.high);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 128.0f, l.low);
}

void test_gaps_without_pulse_are_skipped() {
    const std::vector<float> s = {0, 120, 0, 0, 130, 0};
    const pulse::Level l = pulse::levelOf(s, 0, 6, 1);
    TEST_ASSERT_TRUE(l.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 125.0f, l.mean);
}

void test_only_the_range_counts() {
    const std::vector<float> s = {90, 90, 90, 130, 132, 134};
    const pulse::Level l = pulse::levelOf(s, 3, 6, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 132.0f, l.mean);
}

void test_range_is_clamped() {
    const std::vector<float> s = {130, 134};
    const pulse::Level l = pulse::levelOf(s, -5, 99, 1);
    TEST_ASSERT_TRUE(l.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 132.0f, l.mean);
}

void test_too_little_pulse_is_not_valid() {
    const std::vector<float> s = {0, 0, 128, 0, 129};
    TEST_ASSERT_FALSE(pulse::levelOf(s, 0, 5, 3).valid);
    TEST_ASSERT_TRUE(pulse::levelOf(s, 0, 5, 2).valid);
}

void test_no_pulse_is_never_valid() {
    const std::vector<float> s = {0, 0, 0};
    TEST_ASSERT_FALSE(pulse::levelOf(s, 0, 3, 0).valid);
    TEST_ASSERT_FALSE(pulse::levelOf({}, 0, 0, 0).valid);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mean_and_band_of_steady_pulse);
    RUN_TEST(test_band_between_neighbours);
    RUN_TEST(test_short_spike_does_not_widen_the_band);
    RUN_TEST(test_gaps_without_pulse_are_skipped);
    RUN_TEST(test_only_the_range_counts);
    RUN_TEST(test_range_is_clamped);
    RUN_TEST(test_too_little_pulse_is_not_valid);
    RUN_TEST(test_no_pulse_is_never_valid);
    return UNITY_END();
}
