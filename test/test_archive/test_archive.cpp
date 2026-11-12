// The history of kept walks: one CSV line per walk, read back and merged.
#include <unity.h>

#include <string>
#include <vector>

#include "archive.h"
#include "walk_tracker.h"

using archive::Entry;

namespace {

constexpr int64_t k2026_11_12_0815 = 1794471300;  // 2026-11-12T08:15:00Z

Entry sample(int64_t start = k2026_11_12_0815) {
    Entry e;
    e.startEpoch = start;
    e.endEpoch = start + 3725;
    e.walkSeconds = 3600;
    e.steps = 6120;
    e.distanceM = 4512;
    e.elevationGainM = 361;
    e.pulseSeconds = 3590;
    e.zoneSeconds = {40, 900, 2400, 250, 0, 0};
    e.averageBpm = 121;
    e.maxBpm = 141;
    return e;
}

void assertSame(const Entry& a, const Entry& b) {
    TEST_ASSERT_EQUAL_INT64(a.startEpoch, b.startEpoch);
    TEST_ASSERT_EQUAL_INT64(a.endEpoch, b.endEpoch);
    TEST_ASSERT_EQUAL_UINT32(a.walkSeconds, b.walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(a.steps, b.steps);
    TEST_ASSERT_EQUAL_UINT32(a.distanceM, b.distanceM);
    TEST_ASSERT_EQUAL_UINT32(a.elevationGainM, b.elevationGainM);
    TEST_ASSERT_EQUAL_UINT32(a.pulseSeconds, b.pulseSeconds);
    for (size_t i = 0; i < a.zoneSeconds.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(a.zoneSeconds[i], b.zoneSeconds[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(a.averageBpm, b.averageBpm);
    TEST_ASSERT_EQUAL_UINT16(a.maxBpm, b.maxBpm);
}

}  // namespace

void test_line_has_readable_times_and_all_columns() {
    TEST_ASSERT_EQUAL_STRING(
        "2026-11-12T08:15:00Z,2026-11-12T09:17:05Z,3600,6120,4512,361,3590,40,900,2400,250,0,0,"
        "121,141",
        archive::formatEntry(sample()).c_str());
}

void test_line_reads_back() {
    const auto e = archive::parseEntry(archive::formatEntry(sample()));
    TEST_ASSERT_TRUE(e.has_value());
    assertSame(sample(), *e);
}

void test_walk_without_strap_leaves_pulse_empty() {
    Entry e = sample();
    e.pulseSeconds = 0;
    e.zoneSeconds = {};
    e.averageBpm = 0;
    e.maxBpm = 0;
    const std::string line = archive::formatEntry(e);
    TEST_ASSERT_EQUAL_STRING(
        "2026-11-12T08:15:00Z,2026-11-12T09:17:05Z,3600,6120,4512,361,0,0,0,0,0,0,0,,",
        line.c_str());
    const auto back = archive::parseEntry(line);
    TEST_ASSERT_TRUE(back.has_value());
    assertSame(e, *back);
}

void test_header_and_damaged_lines_are_skipped() {
    const std::string line = archive::formatEntry(sample());
    TEST_ASSERT_FALSE(archive::parseEntry(archive::kHeader).has_value());
    TEST_ASSERT_FALSE(archive::parseEntry("").has_value());
    // Cut short by a power loss.
    TEST_ASSERT_FALSE(archive::parseEntry(line.substr(0, 40)).has_value());
    TEST_ASSERT_FALSE(archive::parseEntry("2026-11-12T08:15:00Z,2026-11-12T09:17:05Z,36x0,6120,"
                                          "4512,361,3590,40,900,2400,250,0,0,121,141")
                          .has_value());
    // Ends before it starts.
    TEST_ASSERT_FALSE(archive::parseEntry("2026-11-12T09:17:05Z,2026-11-12T08:15:00Z,3600,6120,"
                                          "4512,361,3590,40,900,2400,250,0,0,121,141")
                          .has_value());
}

void test_columns_added_later_are_ignored() {
    const auto e = archive::parseEntry(archive::formatEntry(sample()) + ",7,new");
    TEST_ASSERT_TRUE(e.has_value());
    assertSame(sample(), *e);
}

void test_windows_line_ending_is_read() {
    TEST_ASSERT_TRUE(archive::parseEntry(archive::formatEntry(sample()) + "\r").has_value());
}

void test_whole_file_reads_back() {
    const std::vector<Entry> in = {sample(), sample(k2026_11_12_0815 + 7200)};
    const std::string csv = archive::formatAll(in);
    TEST_ASSERT_EQUAL(0, csv.find(std::string(archive::kHeader) + "\n"));
    const std::vector<Entry> out = archive::parseAll(csv + "2026-11-12T1");  // half a line
    TEST_ASSERT_EQUAL(2, out.size());
    assertSame(in[0], out[0]);
    assertSame(in[1], out[1]);
}

void test_from_summary_rounds_and_copies_pulse() {
    walk::Summary s;
    s.walkMs = 3600400;
    s.steps = 6120;
    s.distanceM = 4511.6f;
    s.elevationGainM = 360.5f;
    s.averageHeartRateBpm = 120.6f;
    s.maxHeartRateBpm = 141;
    s.pulseSeconds = 3590;
    s.zoneSeconds = {40, 900, 2400, 250, 0, 0};
    const Entry e = archive::fromSummary(s, k2026_11_12_0815, k2026_11_12_0815 + 3725);
    assertSame(sample(), e);
}

void test_from_summary_without_strap() {
    walk::Summary s;
    s.walkMs = 600000;
    const Entry e = archive::fromSummary(s, k2026_11_12_0815, k2026_11_12_0815 + 600);
    TEST_ASSERT_EQUAL_UINT16(0, e.averageBpm);
    TEST_ASSERT_EQUAL_UINT16(0, e.maxBpm);
    TEST_ASSERT_EQUAL_UINT32(0, e.pulseSeconds);
}

void test_merge_adds_missing_walks_in_start_order() {
    std::vector<Entry> entries = {sample(k2026_11_12_0815 + 86400)};
    const std::vector<Entry> seed = {sample(k2026_11_12_0815), sample(k2026_11_12_0815 - 86400)};
    TEST_ASSERT_EQUAL(2, archive::merge(entries, seed));
    TEST_ASSERT_EQUAL(3, entries.size());
    TEST_ASSERT_EQUAL_INT64(k2026_11_12_0815 - 86400, entries[0].startEpoch);
    TEST_ASSERT_EQUAL_INT64(k2026_11_12_0815, entries[1].startEpoch);
    TEST_ASSERT_EQUAL_INT64(k2026_11_12_0815 + 86400, entries[2].startEpoch);
}

// The same walk timed twice (live and from its log) may differ by a second.
void test_merge_skips_walks_already_there() {
    std::vector<Entry> entries = {sample()};
    const std::vector<Entry> seed = {sample(k2026_11_12_0815 + 1), sample(k2026_11_12_0815 + 1)};
    TEST_ASSERT_EQUAL(0, archive::merge(entries, seed));
    TEST_ASSERT_EQUAL(1, entries.size());
    // Merging the same seed again changes nothing either.
    TEST_ASSERT_EQUAL(0, archive::merge(entries, seed));
}

void test_merge_keeps_walks_a_minute_apart() {
    std::vector<Entry> entries = {sample()};
    TEST_ASSERT_EQUAL(1, archive::merge(entries, {sample(k2026_11_12_0815 + 60)}));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_has_readable_times_and_all_columns);
    RUN_TEST(test_line_reads_back);
    RUN_TEST(test_walk_without_strap_leaves_pulse_empty);
    RUN_TEST(test_header_and_damaged_lines_are_skipped);
    RUN_TEST(test_columns_added_later_are_ignored);
    RUN_TEST(test_windows_line_ending_is_read);
    RUN_TEST(test_whole_file_reads_back);
    RUN_TEST(test_from_summary_rounds_and_copies_pulse);
    RUN_TEST(test_from_summary_without_strap);
    RUN_TEST(test_merge_adds_missing_walks_in_start_order);
    RUN_TEST(test_merge_skips_walks_already_there);
    RUN_TEST(test_merge_keeps_walks_a_minute_apart);
    return UNITY_END();
}
