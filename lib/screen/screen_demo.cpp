#include "screen_demo.h"

#include <cmath>

namespace screen {
namespace demo {

namespace {

struct Key {
    float minute;
    float bpm;
};

// A walk at 3.5 -> 4.5 -> 4.7 km/h and 3 -> 8 -> 11 % incline, the pulse
// following `keys` with a little natural wobble.
walk::History makeWalk(uint32_t seconds, const std::vector<Key>& keys) {
    walk::History h;
    for (uint32_t s = 0; s < seconds; ++s) {
        const float min = s / 60.0f;
        float bpm = keys.back().bpm;
        for (size_t k = 1; k < keys.size(); ++k) {
            if (min <= keys[k].minute) {
                const float f = (min - keys[k - 1].minute) / (keys[k].minute - keys[k - 1].minute);
                bpm = keys[k - 1].bpm + f * (keys[k].bpm - keys[k - 1].bpm);
                break;
            }
        }
        bpm += 1.2f * std::sin(s / 23.0f) + 0.8f * std::sin(s / 7.0f);
        const float speed = min < 3 ? 3.5f : min < 10 ? 4.5f : 4.7f;
        const float incline = min < 5 ? 3.0f : min < 17 ? 8.0f : 11.0f;
        h.push(static_cast<uint16_t>(std::lround(bpm)), speed, incline);
    }
    return h;
}

// A walk from h1:m1 to h2:m2 with this much walking (0: the walk shown, filled
// in at the end).
day::StripSegment at(int h1, int m1, int h2, int m2, uint32_t walkSeconds = 0) {
    day::StripSegment s;
    s.startSec = h1 * 3600 + m1 * 60;
    s.endSec = h2 * 3600 + m2 * 60;
    s.walkSeconds = walkSeconds;
    return s;
}

const std::vector<Key> kSteady = {{0, 92},   {2, 100},  {5, 112},  {8, 120},
                                  {12, 124}, {17, 127}, {19, 133}, {21, 139},
                                  {22, 141}, {24, 137}, {27, 133}, {32.3f, 131}};

std::vector<Key> endingAt(float bpm) {
    std::vector<Key> keys(kSteady.begin(), kSteady.end() - 1);
    keys.push_back({29.5f, (133 + bpm) / 2});
    keys.push_back({32.3f, bpm});
    return keys;
}

// Steps at about 105 a minute, a usual walking pace.
uint32_t stepsOver(uint32_t seconds) { return seconds * 105 / 60; }

constexpr uint32_t kWalkSeconds = 32 * 60 + 14;
constexpr uint32_t kMorningWalks = 1750 + 1300;  // 7:10-7:40 and 9:30-9:52

}  // namespace

const char* sceneName(int i) {
    static const char* const kNames[kSceneCount] = {
        "walk",     "over_ceiling", "interval",   "no_treadmill",
        "no_strap", "goal_met",     "fresh_walk", "pending_upload",
    };
    return i >= 0 && i < kSceneCount ? kNames[i] : "?";
}

Model scene(int i, uint32_t goalSeconds) {
    Model m;
    m.goalSeconds = goalSeconds;
    m.clock = "14:32";
    m.treadmillUp = true;
    m.walking = true;
    m.pulseUp = true;
    m.speedKmh = 4.7f;
    m.inclinePct = 11.0f;

    std::vector<Key> keys = kSteady;
    uint32_t seconds = kWalkSeconds;
    m.bpm = 131;
    m.today.segments = {at(7, 10, 7, 40, 1750), at(9, 30, 9, 52, 1300), at(14, 0, 14, 32)};
    m.today.nowSec = 14 * 3600 + 32 * 60;
    uint32_t earlier = kMorningWalks;

    switch (i) {
        case 1:  // over the ceiling: zone 3
            m.bpm = 142;
            keys = endingAt(142);
            break;
        case 2:  // the hard part of an interval: above VT2
            m.bpm = 168;
            keys = endingAt(168);
            break;
        case 3:  // treadmill link lost, strap still on
            m.treadmillUp = false;
            m.bpm = 128;
            break;
        case 4:  // strap off
            m.pulseUp = false;
            break;
        case 5:  // the goal is met: second lap; a short walk right after a longer
                 // one (its minutes give way) and one under a minute (none shown)
            m.today.segments = {at(7, 10, 7, 40, 1750), at(9, 30, 9, 52, 1300),
                                at(9, 55, 9, 58, 170),  at(11, 0, 11, 45, 2700),
                                at(12, 30, 12, 50, 1200), at(13, 20, 13, 21, 40),
                                at(14, 0, 14, 32)};
            earlier = kMorningWalks + 170 + 2700 + 1200 + 40;
            break;
        case 6:  // three minutes into a walk
            seconds = 190;
            keys = {{0, 92}, {3.2f, 108}};
            m.bpm = 108;
            m.speedKmh = 3.5f;
            m.inclinePct = 3.0f;
            m.today.segments = {at(7, 10, 7, 40, 1750), at(9, 30, 9, 52, 1300), at(14, 0, 14, 3)};
            m.today.nowSec = 14 * 3600 + 3 * 60;
            m.clock = "14:03";
            break;
        case 7:  // evening, treadmill off, two walks waiting for Wi-Fi
            m.treadmillUp = false;
            m.walking = false;
            m.pulseUp = false;
            m.upload = Upload::Pending;
            m.pendingWalks = 2;
            m.clock = "18:05";
            m.today.nowSec = 18 * 3600 + 5 * 60;
            break;
        default:
            break;
    }

    const walk::History h = makeWalk(seconds, keys);
    m.walkSeconds = h.seconds();
    m.walkSteps = stepsOver(m.walkSeconds);
    m.columns = h.columns(kChartColumns, kChartMinSpanS);
    m.chartSpanS = h.spanSeconds(kChartMinSpanS);
    m.today.segments.back().walkSeconds = m.walkSeconds;
    m.today.totalSeconds = earlier + m.walkSeconds;
    m.today.totalSteps = stepsOver(m.today.totalSeconds);
    m.today.walkCount = static_cast<int>(m.today.segments.size());
    return m;
}

stats::Window statsScene(int i, uint32_t goalSeconds) {
    constexpr int64_t kNov12 = 1794441600;  // 2026-11-12T00:00:00Z
    constexpr int64_t kDay = 24 * 3600;
    std::vector<archive::Entry> walks;
    // A walk at noon `daysAgo` days before the 12th.
    auto walk = [&](int daysAgo, uint32_t minutes, uint32_t pulseMin = 0,
                    std::array<uint32_t, pulse::kZoneCount + 1> zoneMin = {}) {
        archive::Entry e;
        e.startEpoch = kNov12 - daysAgo * kDay + 12 * 3600;
        e.endEpoch = e.startEpoch + minutes * 60;
        e.walkSeconds = minutes * 60;
        e.pulseSeconds = pulseMin * 60;
        for (size_t z = 0; z < zoneMin.size(); ++z) e.zoneSeconds[z] = zoneMin[z] * 60;
        walks.push_back(e);
    };
    int64_t start;
    if (i == 0) {  // the history began yesterday
        walk(1, 45);
        walk(1, 38, 38, {2, 12, 24, 0, 0, 0});
        walk(0, 70);
        walk(0, 30, 30, {16, 14, 0, 0, 0, 0});
        start = kNov12 - kDay;
    } else {
        const uint32_t plain[14] = {95, 130, 0, 110, 125, 60, 140, 118, 0, 85, 150, 122, 45, 70};
        for (int d = 0; d < 14; ++d) {
            if (plain[d] > 0) walk(13 - d, plain[d]);
        }
        walk(10, 45, 45, {2, 8, 35, 0, 0, 0});    // base, zone 2
        walk(7, 50, 50, {1, 6, 30, 10, 3, 0});    // drifting into zone 3
        walk(3, 40, 40, {1, 5, 20, 6, 4, 4});     // intervals
        walk(0, 35, 35, {1, 4, 28, 2, 0, 0});
        start = kNov12 - 60 * kDay;
    }
    return stats::lastDays(walks, nullptr, start, kNov12 + 21 * 3600 + 40 * 60, 0, goalSeconds);
}

}  // namespace demo
}  // namespace screen
