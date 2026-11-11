// Parser for the Bluetooth Heart Rate Service "Heart Rate Measurement"
// characteristic (UUID 0x2A37), as sent by chest straps such as the Polar H10.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace heartrate {

struct Measurement {
    uint8_t flags = 0;
    uint16_t bpm = 0;
    // Present only if the strap reports skin contact: false = strap not touching skin.
    std::optional<bool> sensorContact;
    std::optional<uint16_t> energyExpendedKj;
    std::vector<float> rrIntervalsMs;  // beat-to-beat intervals, oldest first

    // A reading worth showing: the strap is on and measured a pulse.
    bool valid() const { return bpm > 0 && sensorContact.value_or(true); }
};

// Decodes a raw notification payload. Returns std::nullopt when the payload is
// shorter than its flags announce.
std::optional<Measurement> parseMeasurement(const uint8_t* data, size_t len);

}  // namespace heartrate
