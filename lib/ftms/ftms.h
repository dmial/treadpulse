// Parser for the Bluetooth Fitness Machine Service (FTMS) "Treadmill Data"
// characteristic (UUID 0x2ACD). Pure C++, no Arduino dependencies, so it can be
// unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ftms {

// One decoded Treadmill Data notification. A treadmill may split its data over
// several notifications with different flag sets, so every field is optional.
struct TreadmillData {
    uint16_t flags = 0;

    std::optional<float> speedKmh;          // instantaneous speed
    std::optional<float> averageSpeedKmh;
    std::optional<uint32_t> totalDistanceM;  // cumulative since the treadmill workout started
    std::optional<float> inclinePct;
    std::optional<float> rampAngleDeg;
    std::optional<float> positiveElevationM;
    std::optional<float> negativeElevationM;
    std::optional<float> instantPaceKmMin;
    std::optional<float> averagePaceKmMin;
    std::optional<uint16_t> totalEnergyKcal;
    std::optional<uint16_t> energyPerHourKcal;
    std::optional<uint8_t> energyPerMinuteKcal;
    std::optional<uint8_t> heartRateBpm;
    std::optional<float> metabolicEquivalent;
    std::optional<uint16_t> elapsedTimeS;
    std::optional<uint16_t> remainingTimeS;
    std::optional<int16_t> forceOnBeltN;
    std::optional<int16_t> powerOutputW;
};

// Decodes a raw notification payload. Returns std::nullopt when the payload is
// shorter than its flags announce. Trailing bytes behind the known fields
// (vendor extensions, reserved flag bits) are ignored.
std::optional<TreadmillData> parseTreadmillData(const uint8_t* data, size_t len);

// Last known value of every metric the display and logger care about.
// Notifications only carry a subset of fields, so new values are merged in.
struct TreadmillState {
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    std::optional<uint32_t> totalDistanceM;
    std::optional<uint16_t> elapsedTimeS;
    std::optional<uint16_t> totalEnergyKcal;

    void merge(const TreadmillData& d);
};

}  // namespace ftms
