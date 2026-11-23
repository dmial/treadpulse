// The last kept walk as the screen shows it between walks: its time, steps,
// time by pulse zone and chart. Kept in flash, so a restart brings it back.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pulse_zones.h"
#include "walk_history.h"

namespace walk {

struct LastWalk {
    uint32_t walkSeconds = 0;
    uint32_t steps = 0;
    // [0] below zone 1 (rest), [1..5] the zones, as the screen counts them.
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};
};

std::vector<uint8_t> encodeLastWalk(const LastWalk& walk, const History& chart);
// False for bytes that are not a last walk in this format (a damaged file, a
// future format); `walk` and `chart` are then left empty.
bool decodeLastWalk(const uint8_t* data, size_t len, LastWalk& walk, History& chart);

}  // namespace walk
