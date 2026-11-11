// Heart rate zones: which zone a pulse is in, where it sits on the on-screen
// zone scale, how far it is from the next line to watch, and when it crosses
// one of those lines upwards (for a beep).
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <array>

namespace pulse {

constexpr int kZoneCount = 5;

// Zone borders in bpm, ascending: zone N (1..5) spans borders[N-1]..borders[N].
// A pulse exactly on a border belongs to the upper zone.
using Borders = std::array<float, kZoneCount + 1>;

// 0 below zone 1 (rest), 1..5 inside a zone; above the last border stays 5.
int zoneOf(const Borders& b, float bpm);

// Marker position on a scale of five equal segments, 0..1: the zone's segment
// plus how far into the zone the pulse is. 0 below zone 1, 1 above zone 5.
float scalePosition(const Borders& b, float bpm);

// The last whole bpm still under a line: 135 for 135.3, 134 for 135.0.
int lastBpmUnder(float line);

// The hint under the pulse: how far it is from the next line to watch.
struct Hint {
    enum class Kind {
        BelowCeiling,   // under the ceiling: "7 below threshold"
        BelowInterval,  // over the ceiling, under the interval line: "12 below threshold"
        AboveInterval,  // over the interval line: "3 above threshold"
    };
    Kind kind = Kind::BelowCeiling;
    int line = 0;      // the line in whole bpm, as lastBpmUnder() gives it
    int distance = 0;  // bpm left to the line, or bpm past it; never negative
};
Hint hintFor(int bpm, float ceiling, float interval);

// Reports a crossing once when the pulse goes over a line, and again only after
// it has dropped `rearm` bpm under that line, so a pulse wobbling on a line
// does not beep every second.
class LineWatcher {
   public:
    enum Crossing { kNone = 0, kCeiling = 1, kInterval = 2 };

    LineWatcher(float ceiling, float interval, float rearm = 3.0f);

    // Call once per pulse reading. `active` false (not walking, or no pulse)
    // forgets the state: the first reading after it never counts as a crossing,
    // so putting the strap on mid-interval stays quiet. When one jump crosses
    // both lines, the higher one is reported.
    Crossing update(bool active, int bpm);

   private:
    enum class State { Unknown, Armed, Fired };
    struct Line {
        float bpm;
        State state = State::Unknown;
    };
    bool step(Line& l, int bpm);

    Line ceiling_;
    Line interval_;
    float rearm_;
};

}  // namespace pulse
