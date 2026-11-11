#include "pulse_zones.h"

#include <cmath>

namespace pulse {

int zoneOf(const Borders& b, float bpm) {
    if (bpm < b[0]) return 0;
    for (int z = 1; z < kZoneCount; ++z) {
        if (bpm < b[z]) return z;
    }
    return kZoneCount;
}

float scalePosition(const Borders& b, float bpm) {
    const int z = zoneOf(b, bpm);
    if (z == 0) return 0.0f;
    float frac = (bpm - b[z - 1]) / (b[z] - b[z - 1]);
    if (frac > 1.0f) frac = 1.0f;  // above the last border
    return (static_cast<float>(z - 1) + frac) / kZoneCount;
}

int lastBpmUnder(float line) { return static_cast<int>(std::ceil(line)) - 1; }

Hint hintFor(int bpm, float ceiling, float interval) {
    Hint h;
    if (bpm < ceiling) {
        h.kind = Hint::Kind::BelowCeiling;
        h.line = lastBpmUnder(ceiling);
        h.distance = h.line - bpm;
    } else if (bpm < interval) {
        h.kind = Hint::Kind::BelowInterval;
        h.line = lastBpmUnder(interval);
        h.distance = h.line - bpm;
    } else {
        h.kind = Hint::Kind::AboveInterval;
        h.line = lastBpmUnder(interval);
        h.distance = bpm - h.line;
    }
    if (h.distance < 0) h.distance = 0;
    return h;
}

LineWatcher::LineWatcher(float ceiling, float interval, float rearm)
    : ceiling_{ceiling}, interval_{interval}, rearm_(rearm) {}

bool LineWatcher::step(Line& l, int bpm) {
    const float v = static_cast<float>(bpm);
    switch (l.state) {
        case State::Unknown:
            l.state = v < l.bpm ? State::Armed : State::Fired;
            return false;
        case State::Armed:
            if (v >= l.bpm) {
                l.state = State::Fired;
                return true;
            }
            return false;
        case State::Fired:
            if (v <= l.bpm - rearm_) l.state = State::Armed;
            return false;
    }
    return false;
}

LineWatcher::Crossing LineWatcher::update(bool active, int bpm) {
    if (!active) {
        ceiling_.state = State::Unknown;
        interval_.state = State::Unknown;
        return kNone;
    }
    const bool overCeiling = step(ceiling_, bpm);
    const bool overInterval = step(interval_, bpm);
    if (overInterval) return kInterval;
    if (overCeiling) return kCeiling;
    return kNone;
}

}  // namespace pulse
