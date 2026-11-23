// Polar Measurement Data (PMD): Polar's own Bluetooth service, next to the
// standard heart rate one, that streams raw measurements on request. A Polar
// H10 offers ECG and its accelerometer. The board writes a start command to the
// control point; the strap answers there, then sends frames on the data
// characteristic until the link drops. Protocol as in Polar's BLE SDK
// (github.com/polarofficial/polar-ble-sdk). Pure C++, host-testable.
//
// Data frame: measurement type (1 byte), the strap's timestamp of the frame's
// last sample (uint64 ns), frame type (1 byte), samples. The H10 sends ECG as
// signed 24-bit microvolts and the accelerometer as three signed 16-bit
// milli-g values per sample.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pmd {

constexpr const char* kServiceUuid = "FB005C80-02E7-F387-1CAD-8ACD2D8DF0C8";
constexpr const char* kControlUuid = "FB005C81-02E7-F387-1CAD-8ACD2D8DF0C8";
constexpr const char* kDataUuid = "FB005C82-02E7-F387-1CAD-8ACD2D8DF0C8";

enum class Measurement : uint8_t {
    Ecg = 0x00,
    Ppg = 0x01,
    Acc = 0x02,
    Ppi = 0x03,
    Gyro = 0x05,
    Mag = 0x06,
};

// What the board asks the H10 for: its only ECG setting, and the accelerometer
// at a rate that shows steps and posture at a quarter of the top rate's bytes.
constexpr uint16_t kEcgSampleRateHz = 130;
constexpr uint16_t kEcgResolutionBits = 14;
constexpr uint16_t kAccSampleRateHz = 50;
constexpr uint16_t kAccResolutionBits = 16;
constexpr uint16_t kAccRangeG = 8;

// A 229-byte ECG frame needs this ATT MTU; the strap refuses to start below it.
constexpr uint16_t kMinMtu = 232;

std::vector<uint8_t> startEcgCommand();
std::vector<uint8_t> startAccCommand();

constexpr uint8_t kOpStart = 0x02;

// The control point's answer to a command.
struct Reply {
    uint8_t opCode = 0;
    uint8_t measurement = 0;
    uint8_t error = 0;  // 0 = success
};
std::optional<Reply> parseReply(const uint8_t* data, size_t len);
// True when the measurement runs after this reply (started now or already).
bool started(const Reply& r);
const char* errorText(uint8_t error);

// The control point's own value: the measurements the device offers, one bit
// per Measurement. nullopt when the value is not that list.
std::optional<uint8_t> parseFeatures(const uint8_t* data, size_t len);
// "ECG, accelerometer"
std::string featureNames(uint8_t features);

constexpr size_t kFrameHeaderSize = 10;
// The measurement a data frame carries; nullopt for one too short for its header.
std::optional<uint8_t> frameMeasurement(const uint8_t* data, size_t len);

}  // namespace pmd
