// The strap's data file, the queue in front of it and Polar's measurement
// commands. Command bytes follow Polar's BLE SDK; replies are what an H10 sends.
#include <unity.h>

#include <vector>

#include "pmd.h"
#include "record_queue.h"
#include "strap_file.h"

namespace {

std::vector<uint8_t> encode(strap::RecordType type, uint32_t uptimeMs, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out(strap::kMaxRecordSize + 16, 0xEE);
    const size_t n = strap::encodeRecord(type, uptimeMs, payload.data(), payload.size(), out.data());
    out.resize(n);
    return out;
}

void pushByte(strap::RecordQueue& q, uint32_t uptimeMs, uint8_t value) {
    q.push(strap::RecordType::HeartRate, uptimeMs, &value, 1);
}

}  // namespace

void test_file_header() {
    const uint8_t expected[] = {'S', 'T', 'R', 'A', 'P', 0x00, 0x01, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, strap::kFileHeader, sizeof(expected));
}

void test_record_layout() {
    // Heart rate notification at uptime 0x01020304 ms: type, uptime LE, length LE, payload.
    const std::vector<uint8_t> rec = encode(strap::RecordType::HeartRate, 0x01020304, {0x16, 0x48, 0xE3, 0x03});
    const std::vector<uint8_t> expected = {0x01, 0x04, 0x03, 0x02, 0x01, 0x04, 0x00, 0x16, 0x48, 0xE3, 0x03};
    TEST_ASSERT_EQUAL(expected.size(), rec.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), rec.data(), expected.size());
}

void test_record_without_payload() {
    const std::vector<uint8_t> rec = encode(strap::RecordType::Note, 7, {});
    const std::vector<uint8_t> expected = {0x10, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(expected.size(), rec.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), rec.data(), expected.size());
}

void test_long_payload_is_cut() {
    const std::vector<uint8_t> payload(strap::kMaxPayload + 10, 0xAB);
    const std::vector<uint8_t> rec = encode(strap::RecordType::PmdData, 1, payload);
    TEST_ASSERT_EQUAL(strap::kMaxRecordSize, rec.size());
    TEST_ASSERT_EQUAL_UINT8(strap::kMaxPayload & 0xFF, rec[5]);
    TEST_ASSERT_EQUAL_UINT8(strap::kMaxPayload >> 8, rec[6]);
}

void test_queue_keeps_order() {
    strap::Slot slots[4];
    strap::RecordQueue q(slots, 4);
    TEST_ASSERT_TRUE(q.empty());
    pushByte(q, 10, 1);
    pushByte(q, 20, 2);
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_EQUAL_UINT32(10, q.front().uptimeMs);
    TEST_ASSERT_EQUAL_UINT8(1, q.front().payload[0]);
    q.pop();
    TEST_ASSERT_EQUAL_UINT32(20, q.front().uptimeMs);
    q.pop();
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT32(0, q.takeLost());
}

void test_full_queue_drops_the_oldest() {
    strap::Slot slots[3];
    strap::RecordQueue q(slots, 3);
    for (uint8_t i = 1; i <= 5; ++i) pushByte(q, i * 100, i);
    TEST_ASSERT_EQUAL(3, q.size());
    TEST_ASSERT_EQUAL_UINT32(2, q.takeLost());
    TEST_ASSERT_EQUAL_UINT32(0, q.takeLost());
    // The newest three, oldest first, across the end of the ring.
    for (uint8_t i = 3; i <= 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, q.front().payload[0]);
        TEST_ASSERT_EQUAL_UINT32(i * 100, q.front().uptimeMs);
        q.pop();
    }
    TEST_ASSERT_TRUE(q.empty());
}

void test_queue_wraps_around() {
    strap::Slot slots[3];
    strap::RecordQueue q(slots, 3);
    for (uint8_t i = 1; i <= 10; ++i) {
        pushByte(q, i, i);
        TEST_ASSERT_EQUAL_UINT8(i, q.front().payload[0]);
        q.pop();
    }
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT32(0, q.takeLost());
}

void test_oversized_or_unbacked_records_count_as_lost() {
    strap::Slot slots[2];
    strap::RecordQueue q(slots, 2);
    const std::vector<uint8_t> big(strap::kMaxPayload + 1, 0);
    q.push(strap::RecordType::PmdData, 1, big.data(), big.size());
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT32(1, q.takeLost());

    strap::RecordQueue none;
    pushByte(none, 1, 1);
    TEST_ASSERT_TRUE(none.empty());
    TEST_ASSERT_EQUAL_UINT32(1, none.takeLost());
}

void test_full_size_payload_is_kept() {
    strap::Slot slots[1];
    strap::RecordQueue q(slots, 1);
    std::vector<uint8_t> frame(strap::kMaxPayload);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(i);
    q.push(strap::RecordType::PmdData, 5, frame.data(), frame.size());
    TEST_ASSERT_EQUAL(1, q.size());
    TEST_ASSERT_EQUAL_UINT16(strap::kMaxPayload, q.front().length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data(), q.front().payload, frame.size());
}

void test_start_ecg_command() {
    // Start, ECG, sample rate 130 Hz, resolution 14 bits.
    const std::vector<uint8_t> expected = {0x02, 0x00, 0x00, 0x01, 0x82, 0x00, 0x01, 0x01, 0x0E, 0x00};
    const std::vector<uint8_t> cmd = pmd::startEcgCommand();
    TEST_ASSERT_EQUAL(expected.size(), cmd.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), cmd.data(), expected.size());
}

void test_start_acc_command() {
    // Start, accelerometer, 50 Hz, 16 bits, range 8 g.
    const std::vector<uint8_t> expected = {0x02, 0x02, 0x00, 0x01, 0x32, 0x00, 0x01,
                                           0x01, 0x10, 0x00, 0x02, 0x01, 0x08, 0x00};
    const std::vector<uint8_t> cmd = pmd::startAccCommand();
    TEST_ASSERT_EQUAL(expected.size(), cmd.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), cmd.data(), expected.size());
}

void test_reply_to_a_start() {
    const uint8_t ok[] = {0xF0, 0x02, 0x00, 0x00, 0x00};
    auto r = pmd::parseReply(ok, sizeof(ok));
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_UINT8(pmd::kOpStart, r->opCode);
    TEST_ASSERT_EQUAL_UINT8(0x00, r->measurement);
    TEST_ASSERT_TRUE(pmd::started(*r));

    const uint8_t running[] = {0xF0, 0x02, 0x02, 0x06, 0x00};
    r = pmd::parseReply(running, sizeof(running));
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(pmd::started(*r));

    const uint8_t mtu[] = {0xF0, 0x02, 0x00, 0x0A, 0x00};
    r = pmd::parseReply(mtu, sizeof(mtu));
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(pmd::started(*r));
    TEST_ASSERT_EQUAL_STRING("MTU too small", pmd::errorText(r->error));
}

void test_not_a_reply() {
    const uint8_t features[] = {0x0F, 0x05, 0x00};
    TEST_ASSERT_FALSE(pmd::parseReply(features, sizeof(features)).has_value());
    const uint8_t shortReply[] = {0xF0, 0x02, 0x00};
    TEST_ASSERT_FALSE(pmd::parseReply(shortReply, sizeof(shortReply)).has_value());
}

void test_features_of_an_h10() {
    const uint8_t value[] = {0x0F, 0x05, 0x00};
    auto f = pmd::parseFeatures(value, sizeof(value));
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_EQUAL_UINT8(0x05, *f);
    TEST_ASSERT_EQUAL_STRING("ECG, accelerometer", pmd::featureNames(*f).c_str());
    TEST_ASSERT_EQUAL_STRING("none", pmd::featureNames(0).c_str());

    const uint8_t reply[] = {0xF0, 0x02, 0x00, 0x00};
    TEST_ASSERT_FALSE(pmd::parseFeatures(reply, sizeof(reply)).has_value());
}

void test_frame_measurement() {
    std::vector<uint8_t> frame(pmd::kFrameHeaderSize + 3, 0);
    frame[0] = static_cast<uint8_t>(pmd::Measurement::Acc);
    auto m = pmd::frameMeasurement(frame.data(), frame.size());
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_UINT8(0x02, *m);
    TEST_ASSERT_FALSE(pmd::frameMeasurement(frame.data(), pmd::kFrameHeaderSize - 1).has_value());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_file_header);
    RUN_TEST(test_record_layout);
    RUN_TEST(test_record_without_payload);
    RUN_TEST(test_long_payload_is_cut);
    RUN_TEST(test_queue_keeps_order);
    RUN_TEST(test_full_queue_drops_the_oldest);
    RUN_TEST(test_queue_wraps_around);
    RUN_TEST(test_oversized_or_unbacked_records_count_as_lost);
    RUN_TEST(test_full_size_payload_is_kept);
    RUN_TEST(test_start_ecg_command);
    RUN_TEST(test_start_acc_command);
    RUN_TEST(test_reply_to_a_start);
    RUN_TEST(test_not_a_reply);
    RUN_TEST(test_features_of_an_h10);
    RUN_TEST(test_frame_measurement);
    return UNITY_END();
}
