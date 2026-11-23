// The walk squeezed into chart columns.
#include <unity.h>

#include <vector>

#include "last_walk.h"
#include "walk_history.h"

using walk::Column;
using walk::History;

void test_empty_history_has_no_data() {
    History h;
    const auto cols = h.columns(10, 600);
    TEST_ASSERT_EQUAL(10, cols.size());
    for (const Column& c : cols) TEST_ASSERT_FALSE(c.hasData);
}

void test_columns_average_pulse_and_take_last_speed() {
    History h;
    h.push(100, 4.0f, 2.0f);
    h.push(110, 4.5f, 3.0f);
    h.push(120, 5.0f, 4.0f);
    h.push(0, 5.5f, 5.0f);  // strap lost for a second
    const auto cols = h.columns(2, 0);
    TEST_ASSERT_TRUE(cols[0].hasData);
    TEST_ASSERT_EQUAL_FLOAT(105.0f, cols[0].bpm);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, cols[0].speedKmh);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, cols[0].inclinePct);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, cols[1].bpm);  // the gap does not drag it down
    TEST_ASSERT_EQUAL_FLOAT(5.5f, cols[1].speedKmh);
}

void test_column_without_pulse_has_zero_bpm() {
    History h;
    h.push(0, 4.0f, 0.0f);
    h.push(0, 4.0f, 0.0f);
    const auto cols = h.columns(1, 0);
    TEST_ASSERT_TRUE(cols[0].hasData);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cols[0].bpm);
}

void test_short_walk_fills_only_the_left_part() {
    History h;
    for (int i = 0; i < 60; ++i) h.push(120, 4.7f, 11.0f);
    // 60 s on a 600 s chart of 10 columns: only the first column has data.
    const auto cols = h.columns(10, 600);
    TEST_ASSERT_EQUAL_UINT32(600, h.spanSeconds(600));
    TEST_ASSERT_TRUE(cols[0].hasData);
    TEST_ASSERT_FALSE(cols[1].hasData);
}

void test_more_columns_than_seconds_repeats_points() {
    History h;
    h.push(100, 4.0f, 1.0f);
    h.push(140, 5.0f, 2.0f);
    const auto cols = h.columns(4, 0);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, cols[0].bpm);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, cols[1].bpm);
    TEST_ASSERT_EQUAL_FLOAT(140.0f, cols[2].bpm);
    TEST_ASSERT_EQUAL_FLOAT(140.0f, cols[3].bpm);
}

void test_full_history_is_halved_and_keeps_the_time() {
    History h(4);
    for (int i = 0; i < 9; ++i) h.push(static_cast<uint16_t>(100 + i), 4.0f + i, 0.0f);
    // 4 points of 1 s halve into 2 of 2 s; 9 readings make 4 points of 2 s + 1 pending.
    TEST_ASSERT_EQUAL_UINT32(9, h.seconds());
    const auto cols = h.columns(4, 0);
    // Points: 101 (100+101), 103 (102+103), 105 (104+105), 107 (106+107).
    TEST_ASSERT_TRUE(cols[0].hasData);
    TEST_ASSERT_EQUAL_FLOAT(102.0f, cols[0].bpm);     // first two points
    TEST_ASSERT_EQUAL_FLOAT(7.0f, cols[0].speedKmh);  // end of the second point
    TEST_ASSERT_TRUE(cols[3].hasData);
    TEST_ASSERT_EQUAL_FLOAT(107.0f, cols[3].bpm);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, cols[3].speedKmh);
}

void test_trim_drops_the_idle_tail() {
    History h;
    for (int i = 0; i < 10; ++i) h.push(120, 4.0f, 0.0f);
    for (int i = 0; i < 5; ++i) h.push(110, 0.0f, 0.0f);  // standing after the walk
    h.trimTo(10);
    TEST_ASSERT_EQUAL_UINT32(10, h.seconds());
    TEST_ASSERT_EQUAL_FLOAT(4.0f, h.columns(1, 0)[0].speedKmh);
    h.trimTo(20);  // longer than the history: nothing changes
    TEST_ASSERT_EQUAL_UINT32(10, h.seconds());
}

void test_clear_starts_over() {
    History h(4);
    for (int i = 0; i < 9; ++i) h.push(120, 4.0f, 0.0f);
    h.clear();
    TEST_ASSERT_EQUAL_UINT32(0, h.seconds());
    h.push(130, 4.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(1, h.seconds());
    TEST_ASSERT_EQUAL_FLOAT(130.0f, h.columns(1, 0)[0].bpm);
}

// The same chart, column by column.
void assertSameChart(const History& a, const History& b) {
    TEST_ASSERT_EQUAL_UINT32(a.seconds(), b.seconds());
    const auto ca = a.columns(7, 0);
    const auto cb = b.columns(7, 0);
    for (size_t i = 0; i < ca.size(); ++i) {
        TEST_ASSERT_EQUAL(ca[i].hasData, cb[i].hasData);
        TEST_ASSERT_EQUAL_FLOAT(ca[i].bpm, cb[i].bpm);
        TEST_ASSERT_EQUAL_FLOAT(ca[i].speedKmh, cb[i].speedKmh);
        TEST_ASSERT_EQUAL_FLOAT(ca[i].inclinePct, cb[i].inclinePct);
    }
}

void test_bytes_bring_the_chart_back() {
    History h(4);  // halved twice below, with readings pending
    for (int i = 0; i < 13; ++i) h.push(i % 3 ? 100 + i : 0, 3.0f + i * 0.1f, i - 2.0f);
    std::vector<uint8_t> bytes;
    h.appendBytes(bytes);

    History back(4);
    TEST_ASSERT_TRUE(back.readBytes(bytes.data(), bytes.size()));
    assertSameChart(h, back);
    // It goes on as the original would.
    h.push(150, 5.0f, 1.0f);
    back.push(150, 5.0f, 1.0f);
    assertSameChart(h, back);
}

void test_damaged_bytes_leave_the_history_empty() {
    History h;
    for (int i = 0; i < 5; ++i) h.push(120, 4.0f, 0.0f);
    std::vector<uint8_t> bytes;
    h.appendBytes(bytes);

    History back;
    back.push(90, 2.0f, 0.0f);
    TEST_ASSERT_FALSE(back.readBytes(bytes.data(), bytes.size() - 1));  // cut short
    TEST_ASSERT_EQUAL_UINT32(0, back.seconds());
    History small(2);  // more points than it may hold
    TEST_ASSERT_FALSE(small.readBytes(bytes.data(), bytes.size()));
}

void test_last_walk_comes_back_with_its_chart() {
    walk::LastWalk w;
    w.walkSeconds = 1530;
    w.steps = 2448;
    w.zoneSeconds = {60, 400, 700, 300, 70, 0};
    History chart;
    for (int i = 0; i < 90; ++i) chart.push(110 + i % 20, 3.6f, 1.0f);
    const std::vector<uint8_t> bytes = walk::encodeLastWalk(w, chart);

    walk::LastWalk w2;
    History chart2;
    TEST_ASSERT_TRUE(walk::decodeLastWalk(bytes.data(), bytes.size(), w2, chart2));
    TEST_ASSERT_EQUAL_UINT32(1530, w2.walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(2448, w2.steps);
    for (size_t z = 0; z < w.zoneSeconds.size(); ++z) TEST_ASSERT_EQUAL_UINT32(w.zoneSeconds[z], w2.zoneSeconds[z]);
    assertSameChart(chart, chart2);
}

void test_other_bytes_are_no_last_walk() {
    walk::LastWalk w;
    w.walkSeconds = 600;
    History chart;
    chart.push(120, 4.0f, 0.0f);
    std::vector<uint8_t> bytes = walk::encodeLastWalk(w, chart);
    bytes[2] = 2;  // a format this firmware does not know

    walk::LastWalk w2;
    History chart2;
    TEST_ASSERT_FALSE(walk::decodeLastWalk(bytes.data(), bytes.size(), w2, chart2));
    TEST_ASSERT_EQUAL_UINT32(0, w2.walkSeconds);
    TEST_ASSERT_EQUAL_UINT32(0, chart2.seconds());
    TEST_ASSERT_FALSE(walk::decodeLastWalk(nullptr, 0, w2, chart2));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_history_has_no_data);
    RUN_TEST(test_columns_average_pulse_and_take_last_speed);
    RUN_TEST(test_column_without_pulse_has_zero_bpm);
    RUN_TEST(test_short_walk_fills_only_the_left_part);
    RUN_TEST(test_more_columns_than_seconds_repeats_points);
    RUN_TEST(test_full_history_is_halved_and_keeps_the_time);
    RUN_TEST(test_trim_drops_the_idle_tail);
    RUN_TEST(test_clear_starts_over);
    RUN_TEST(test_bytes_bring_the_chart_back);
    RUN_TEST(test_damaged_bytes_leave_the_history_empty);
    RUN_TEST(test_last_walk_comes_back_with_its_chart);
    RUN_TEST(test_other_bytes_are_no_last_walk);
    return UNITY_END();
}
