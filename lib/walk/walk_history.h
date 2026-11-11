// The current walk second by second, kept in memory for the on-screen chart and
// squeezed into as many columns as the chart is wide.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace walk {

// One chart column: the pulse averaged over its time, the speed and incline at
// its end. `hasData` is false for columns past the end of the walk.
struct Column {
    bool hasData = false;
    float bpm = 0.0f;  // 0 = no pulse during this column
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
};

class History {
   public:
    // ~6 bytes per point; when full, neighbouring points are merged in pairs so
    // a walk of any length fits (6 h at one point per second by default).
    explicit History(size_t maxPoints = 6 * 3600)
        : maxPoints_(maxPoints < 2 ? 2 : (maxPoints + 1) / 2 * 2) {}  // even, so halving is exact

    void clear();
    // One reading per tick (1 s) while a walk is on. bpm 0 = no pulse.
    void push(uint16_t bpm, float speedKmh, float inclinePct);
    // Keeps only the first `seconds`: a walk ends after minutes of standing
    // still, and that tail is not part of it.
    void trimTo(uint32_t seconds);

    // Time covered so far, in seconds.
    uint32_t seconds() const;
    // What the chart spans: the walk, but at least `minSpanS` so a fresh walk
    // starts at the left edge instead of stretching over the whole width.
    uint32_t spanSeconds(uint32_t minSpanS) const;
    // The walk in `count` columns over spanSeconds(minSpanS).
    std::vector<Column> columns(int count, uint32_t minSpanS) const;

   private:
    struct Point {
        uint16_t bpm = 0;
        uint16_t speedCenti = 0;
        int16_t inclineDeci = 0;
    };
    // Readings not yet making up a whole point (after the history was halved).
    struct Pending {
        uint32_t count = 0;
        uint32_t bpmSum = 0;
        uint32_t bpmCount = 0;
        Point last;
    };

    void halve();

    size_t maxPoints_;
    std::vector<Point> points_;
    uint32_t pointSeconds_ = 1;
    Pending pending_;
};

}  // namespace walk
