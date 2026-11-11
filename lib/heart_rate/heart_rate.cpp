#include "heart_rate.h"

namespace heartrate {

namespace {

// Flag bits of the Heart Rate Measurement characteristic (Heart Rate Service v1.0).
constexpr uint8_t kValueIsUint16 = 1u << 0;
constexpr uint8_t kContactDetected = 1u << 1;
constexpr uint8_t kContactSupported = 1u << 2;
constexpr uint8_t kEnergyExpended = 1u << 3;
constexpr uint8_t kRrIntervals = 1u << 4;

uint16_t u16At(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

std::optional<Measurement> parseMeasurement(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 2) return std::nullopt;

    Measurement m;
    m.flags = data[0];
    size_t pos = 1;

    if (m.flags & kValueIsUint16) {
        if (pos + 2 > len) return std::nullopt;
        m.bpm = u16At(data + pos);
        pos += 2;
    } else {
        m.bpm = data[pos];
        pos += 1;
    }

    if (m.flags & kContactSupported) m.sensorContact = (m.flags & kContactDetected) != 0;

    if (m.flags & kEnergyExpended) {
        if (pos + 2 > len) return std::nullopt;
        m.energyExpendedKj = u16At(data + pos);
        pos += 2;
    }

    if (m.flags & kRrIntervals) {
        // RR intervals fill the rest of the payload, 1/1024 s each.
        for (; pos + 2 <= len; pos += 2) {
            m.rrIntervalsMs.push_back(u16At(data + pos) * 1000.0f / 1024.0f);
        }
    }

    return m;
}

}  // namespace heartrate
