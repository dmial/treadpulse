#include "last_walk.h"

namespace walk {

namespace {

// "LW", format 1, a zero byte; then the numbers, little-endian; then the chart
// (History::appendBytes).
constexpr uint8_t kMagic[4] = {'L', 'W', 1, 0};
constexpr size_t kNumbers = 2 + pulse::kZoneCount + 1;
constexpr size_t kHeaderBytes = sizeof(kMagic) + 4 * kNumbers;

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

std::vector<uint8_t> encodeLastWalk(const LastWalk& walk, const History& chart) {
    std::vector<uint8_t> out(kMagic, kMagic + sizeof(kMagic));
    putU32(out, walk.walkSeconds);
    putU32(out, walk.steps);
    for (uint32_t s : walk.zoneSeconds) putU32(out, s);
    chart.appendBytes(out);
    return out;
}

bool decodeLastWalk(const uint8_t* data, size_t len, LastWalk& walk, History& chart) {
    walk = LastWalk();
    chart.clear();
    if (!data || len < kHeaderBytes) return false;
    for (size_t i = 0; i < sizeof(kMagic); ++i) {
        if (data[i] != kMagic[i]) return false;
    }
    LastWalk w;
    const uint8_t* p = data + sizeof(kMagic);
    w.walkSeconds = getU32(p);
    w.steps = getU32(p + 4);
    for (size_t z = 0; z < w.zoneSeconds.size(); ++z) w.zoneSeconds[z] = getU32(p + 8 + 4 * z);
    if (!chart.readBytes(data + kHeaderBytes, len - kHeaderBytes)) return false;
    walk = w;
    return true;
}

}  // namespace walk
